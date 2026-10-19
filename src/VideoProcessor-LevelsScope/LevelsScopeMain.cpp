#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include "LevelsScopeEngine.h"
#include "LevelsScopeLog.h"
#include "LevelsScopePanel.h"
#include <VpTheme.h>
#include <QApplication>
#include <QElapsedTimer>
#include <QSettings>
#include <windows.h>
#include <ConfigEditorCore.h>
#include <ToneProfileRequest.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

// The VP Levels Scope helper. With no arguments it opens the scope window
// (LevelsScopePanel, in his Config theme). With --csv it reads vp.log files
// and one Rendering / DTM profile of a saved VideoProcessor.cfg, and writes a
// CSV row per logged sample: what VP logged, what libplacebo received, and
// where the probe levels landed under the profile and under libplacebo's
// default. Every curve value comes from the Config page's tone engine, so it
// runs from config\ beside VP Renderer (Beta)'s folder, as the Config
// application does.
//
//   VideoProcessorLevelsScope --config <VideoProcessor.cfg> --profile <section>
//       --csv <out.csv> <vp.log> [<vp.log> ...]
//
// <section> as in the cfg, e.g. vprenderer.Default.
namespace
{
    // Fixed probe levels, in nits; each sample adds its own measured peak and
    // PQ average, and the master's black.
    const double FixedProbes[] = { 0.001, 0.01, 0.1, 1.0, 10.0, 100.0 };

    std::string Narrow(const std::wstring& text)
    {
        std::string result;
        for (wchar_t c : text) result.push_back(c < 128 ? static_cast<char>(c) : '?');
        return result;
    }

    std::wstring Widen(const char* text)
    {
        std::wstring result;
        for (; *text; ++text) result.push_back(static_cast<unsigned char>(*text));
        return result;
    }

    void Number(std::FILE* out, double value)
    {
        std::fprintf(out, ",%.6g", value);
    }

    int Usage()
    {
        std::cerr << "usage: VideoProcessorLevelsScope --config <VideoProcessor.cfg> --profile <section>"
                     " --csv <out.csv> <vp.log> [<vp.log> ...]\n";
        return 2;
    }
}

int RunCsv(int argc, char** argv)
{
    std::string config, profile, csv;
    std::vector<std::string> logs;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if ((arg == "--config" || arg == "--profile" || arg == "--csv") && i + 1 < argc)
            (arg == "--config" ? config : arg == "--profile" ? profile : csv) = argv[++i];
        else if (!arg.empty() && arg[0] != '-')
            logs.push_back(arg);
        else
            return Usage();
    }
    if (config.empty() || profile.empty() || csv.empty() || logs.empty())
        return Usage();

    ConfigEditorCore::ConfigDocument document;
    std::wstring error;
    if (!document.Load(Widen(config.c_str()), error))
    {
        std::cerr << "cannot read " << config << ": " << Narrow(error) << "\n";
        return 1;
    }
    bool found = false;
    for (const std::string& section : document.SectionNames())
        found = found || _stricmp(section.c_str(), profile.c_str()) == 0;
    if (!found)
    {
        std::cerr << "no [" << profile << "] in " << config << "\n";
        return 1;
    }
    // As the Config page reads a saved profile: its own value, else built in.
    const ToneCurveProvider::Request request = ToneProfileRequest::From([&](const char* key) {
        return QString::fromLocal8Bit(document.Get(profile.c_str(), key).c_str()).trimmed();
    });

    std::FILE* out = nullptr;
    if (fopen_s(&out, csv.c_str(), "w") != 0 || !out)
    {
        std::cerr << "cannot write " << csv << "\n";
        return 1;
    }
    std::fprintf(out, "file,build,time,dv,measured,detected_peak,detected_avg_pq,detected_avg_linear,"
        "authored,authored_peak,authored_avg,master_black,master_peak,max_cll,max_fall,"
        "curve,input_min,input_max,knee,output_min,output_max,virtual_target,exposure,mastering_clip");
    for (double probe : FixedProbes)
        std::fprintf(out, ",out_%g,default_%g", probe, probe);
    std::fprintf(out, ",out_detected_peak,default_detected_peak,out_detected_avg_pq,default_detected_avg_pq,"
        "out_master_black,default_master_black\n");

    const ToneCurveProvider::Functions& f = ToneCurveProvider::Registered();
    size_t written = 0;
    for (const std::string& path : logs)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            std::cerr << "cannot read " << path << "\n";
            std::fclose(out);
            return 1;
        }
        LevelsScopeLog::Reader reader;
        for (std::string line; std::getline(file, line);)
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            reader.Line(line);
        }
        const std::string name = path.substr(path.find_last_of("\\/") + 1);
        for (const LevelsScopeLog::Sample& sample : reader.Samples())
        {
            std::vector<double> probes(std::begin(FixedProbes), std::end(FixedProbes));
            probes.push_back(sample.detectedPeakNits);
            probes.push_back(sample.detectedPqAverageNits);
            probes.push_back(sample.masteringBlackNits);
            LevelsScopeEngine::Outcome outcome;
            if (!LevelsScopeEngine::Evaluate(request, sample, probes, outcome, error))
            {
                std::cerr << "tone engine: " << Narrow(error) << "\n";
                std::fclose(out);
                return 1;
            }
            const ToneCurveProvider::Result& r = outcome.result;
            std::fprintf(out, "%s,%s,%s,%d,%d", name.c_str(), reader.Build().c_str(), sample.time.c_str(),
                sample.dolbyVision ? 1 : 0, sample.measured ? 1 : 0);
            Number(out, sample.detectedPeakNits);
            Number(out, sample.detectedPqAverageNits);
            Number(out, sample.detectedLinearAverageNits);
            std::fprintf(out, ",%d", sample.authored ? 1 : 0);
            Number(out, sample.authoredPeakNits);
            Number(out, sample.authoredAverageNits);
            Number(out, sample.masteringBlackNits);
            Number(out, sample.masteringPeakNits);
            Number(out, sample.maxCllNits);
            Number(out, sample.maxFallNits);
            std::fprintf(out, ",%s", r.curve.c_str());
            Number(out, f.pqToNits(r.inputMinPq));
            Number(out, f.pqToNits(r.inputMaxPq));
            Number(out, f.pqToNits(r.kneePq));
            Number(out, f.pqToNits(r.outputMinPq));
            Number(out, f.pqToNits(r.outputMaxPq));
            Number(out, r.virtualTargetNits);
            Number(out, r.exposure);
            Number(out, r.masteringClip);
            for (size_t i = 0; i < probes.size(); ++i)
            {
                Number(out, outcome.profileNits[i]);
                Number(out, outcome.defaultNits[i]);
            }
            std::fprintf(out, "\n");
            ++written;
        }
        std::cerr << name << ": " << reader.Samples().size() << " samples ("
                  << (reader.Measured() ? "measured and L1" : "L1 only") << "), build "
                  << (reader.Build().empty() ? "not identified" : reader.Build()) << "\n";
    }
    std::fclose(out);
    std::cerr << written << " rows -> " << csv << "\n";
    return 0;
}

// --snapshot <out> --config <cfg> --profile <section> <vp.log>...: the whole
// window drawn off screen to <out>-inout.png, <out>-input.png and
// <out>-inout-10min.png, with how long each chart took to draw. For checking
// the page without a screen capture; its settings are kept apart and cleared.
int RunSnapshot(int argc, char** argv)
{
    QString out, config, profile;
    QStringList logs;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if ((arg == "--snapshot" || arg == "--config" || arg == "--profile") && i + 1 < argc)
            (arg == "--snapshot" ? out : arg == "--config" ? config : profile) = QString::fromLocal8Bit(argv[++i]);
        else if (!arg.empty() && arg[0] != '-')
            logs << QString::fromLocal8Bit(argv[i]);
        else
            return Usage();
    }
    if (out.isEmpty() || config.isEmpty() || profile.isEmpty() || logs.isEmpty())
        return Usage();
    int result = 0;
    {
        LevelsScopePanel::Panel panel(nullptr, QStringLiteral("Levels Scope Snapshot"), false);
        panel.setAttribute(Qt::WA_DontShowOnScreen, true);
        panel.resize(1400, 900);
        panel.show();
        QElapsedTimer timer;
        timer.start();
        panel.open(config, logs);
        const double loadMs = timer.nsecsElapsed() / 1e6;
        if (!panel.selectProfile(profile))
        {
            std::cerr << "no DTM profile " << profile.toStdString() << " in " << config.toStdString() << "\n";
            result = 1;
        }
        std::cerr << "read and placed: " << loadMs << " ms; " << panel.status().toStdString() << "\n";
        const auto shot = [&](int view, const QString& name, double window) {
            panel.showView(view);
            panel.chart()->setWindow(window);
            QCoreApplication::processEvents();
            QElapsedTimer t;
            t.start();
            const QPixmap image = panel.grab();
            const double ms = t.nsecsElapsed() / 1e6;
            image.save(out + name);
            std::cerr << (out + name).toStdString() << ": chart drawn in " << panel.chart()->lastDrawMs()
                      << " ms, window in " << ms << " ms\n";
        };
        shot(1, QStringLiteral("-inout.png"), 0);
        shot(0, QStringLiteral("-input.png"), 0);
        shot(1, QStringLiteral("-inout-10min.png"), 600);
    }
    QSettings(QStringLiteral("VideoProcessor"), QStringLiteral("Levels Scope Snapshot")).clear();
    return result;
}

int main(int argc, char** argv)
{
    bool snapshot = false;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--csv") == 0)
            return RunCsv(argc, argv);
        snapshot = snapshot || std::strcmp(argv[i], "--snapshot") == 0;
    }
    // A window: drop the console Windows gave a program started from Explorer,
    // keep the terminal's when started from one.
    DWORD processes[2] = {};
    if (!snapshot && GetConsoleProcessList(processes, 2) == 1)
        FreeConsole();
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("VP Levels Scope"));
    QApplication::setOrganizationName(QStringLiteral("VideoProcessor"));
    QApplication::setStyle(VpTheme::CreateStyle());
    app.setStyleSheet(VpTheme::StyleSheet());
    if (snapshot)
        return RunSnapshot(argc, argv);
    LevelsScopePanel::Panel panel;
    panel.setWindowTitle(QStringLiteral("VP Levels Scope"));
    panel.resize(1400, 900);
    panel.show();
    return app.exec();
}
