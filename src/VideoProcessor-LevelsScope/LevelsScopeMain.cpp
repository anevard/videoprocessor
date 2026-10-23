#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include "LevelsScopePanel.h"
#include <LoggedScene.h>
#include <VpLog.h>
#include <VpTheme.h>
#include <QAbstractButton>
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QPointer>
#include <windows.h>
#include <ConfigEditorCore.h>
#include <RenderingBuiltInDefault.h>
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
// runs from config\ beside the drawn renderer's folder, as the Config
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
    }, RenderingBuiltInDefault);

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
        VpLog::Reader reader;
        reader.Read(file);
        const std::string name = path.substr(path.find_last_of("\\/") + 1);
        for (const VpLog::Sample& sample : reader.Samples())
        {
            std::vector<double> probes(std::begin(FixedProbes), std::end(FixedProbes));
            probes.push_back(sample.detectedPeakNits);
            probes.push_back(sample.detectedPqAverageNits);
            probes.push_back(sample.masteringBlackNits);
            LoggedScene::Outcome outcome;
            if (!LoggedScene::Evaluate(request, sample, probes, outcome, error))
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

// --snapshot <out> --config <cfg> --profile <section> [--settings <folder>]
// <vp.log>...: the whole window drawn off screen to <out>-inout.png,
// <out>-input.png and <out>-inout-10min.png, with how long each chart took to
// draw. For checking the page without a screen capture. It reads and writes no
// settings unless --settings names a folder (a throwaway one, for a check).
int RunSnapshot(int argc, char** argv)
{
    QString out, config, profile, settings;
    QStringList logs;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if ((arg == "--snapshot" || arg == "--config" || arg == "--profile" || arg == "--settings") && i + 1 < argc)
            (arg == "--snapshot" ? out : arg == "--config" ? config : arg == "--profile" ? profile : settings) = QString::fromLocal8Bit(argv[++i]);
        else if (!arg.empty() && arg[0] != '-')
            logs << QString::fromLocal8Bit(argv[i]);
        else
            return Usage();
    }
    if (out.isEmpty() || config.isEmpty() || profile.isEmpty() || logs.isEmpty())
        return Usage();
    int result = 0;
    {
        LevelsScopePanel::Panel panel(nullptr, settings, false);
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
        // Zoomed in on brightness: the shadows, 0.0005 to 20 nits.
        panel.chart()->setBrightness(0.0005, 20);
        shot(1, QStringLiteral("-inout-10min-shadows.png"), 600);
    }
    return result;
}

// The window's own timing: every event that takes over 30 ms to handle is
// written to %TEMP%\vp-levels-scope-timing.log with what it was, so a slow
// click or zoom on the operator's PC can be read rather than guessed.
class TimedApplication : public QApplication
{
public:
    TimedApplication(int& argc, char** argv) : QApplication(argc, argv)
    {
        log_.setFileName(QDir::temp().absoluteFilePath(QStringLiteral("vp-levels-scope-timing.log")));
        if (log_.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
            log_.write(QStringLiteral("%1 started\n").arg(QDateTime::currentDateTime().toString(Qt::ISODate)).toUtf8());
    }
    bool notify(QObject* receiver, QEvent* event) override
    {
        QElapsedTimer timer;
        timer.start();
        ++depth_;
        const QEvent::Type type = event->type();
        // The event may delete its receiver (a log chip's close removes the chip):
        // read nothing from it afterwards unless it is still there.
        const char* className = receiver->metaObject()->className();
        const QPointer<QObject> alive(receiver);
        const bool result = QApplication::notify(receiver, event);
        --depth_;
        const double ms = timer.nsecsElapsed() / 1e6;
        if (ms > 30.0 && log_.isOpen())
        {
            QString what = QString::fromLatin1(className);
            if (!alive) what += QStringLiteral(" (deleted by the event)");
            else
            {
                if (!receiver->objectName().isEmpty()) what += QStringLiteral(" ") + receiver->objectName();
                if (auto* button = qobject_cast<QAbstractButton*>(receiver)) what += QStringLiteral(" \"") + button->text() + u'"';
            }
            log_.write(QStringLiteral("%1 %2%3 ms  event %4  %5\n")
                .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz")))
                .arg(QString(depth_ * 2, u' '))
                .arg(ms, 8, 'f', 1).arg(static_cast<int>(type)).arg(what).toUtf8());
            log_.flush();
        }
        return result;
    }

private:
    QFile log_;
    int depth_ = 0;
};

// The scope is a windowed program, so starting it opens no console. Run from a
// terminal for --csv or --snapshot, it writes to that terminal; output
// redirected to a file already reaches the file.
void UseParentConsole()
{
    const bool outGoes = GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) != FILE_TYPE_UNKNOWN;
    const bool errGoes = GetFileType(GetStdHandle(STD_ERROR_HANDLE)) != FILE_TYPE_UNKNOWN;
    if ((outGoes && errGoes) || !AttachConsole(ATTACH_PARENT_PROCESS)) return;
    FILE* stream = nullptr;
    if (!outGoes) freopen_s(&stream, "CONOUT$", "w", stdout);
    if (!errGoes) freopen_s(&stream, "CONOUT$", "w", stderr);
}

int main(int argc, char** argv)
{
    bool snapshot = false;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--csv") == 0)
        {
            UseParentConsole();
            return RunCsv(argc, argv);
        }
        snapshot = snapshot || std::strcmp(argv[i], "--snapshot") == 0;
    }
    if (snapshot) UseParentConsole();
    TimedApplication app(argc, argv);
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
