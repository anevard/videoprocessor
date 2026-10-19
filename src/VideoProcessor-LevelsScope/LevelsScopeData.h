#pragma once
#include "LevelsScopeEngine.h"
#include "LevelsScopeLog.h"
#include <QDateTime>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <algorithm>
#include <fstream>
#include <map>
#include <vector>

// The Levels Scope's data: logged samples on one clock, split into viewing
// sessions and sections, each sample carrying where a profile put its levels.
// The same reading as the web page it replaces (tools/scope in VP_validation):
// sessions break at gaps over 30 minutes; a section is a run of one declared
// source range (DV or not, mastering black and peak) without a gap over 30 s,
// kept when it lasts 2 minutes or more.
namespace LevelsScopeData
{
    // The fixed probe levels of the In vs out view, in nits.
    constexpr double Probes[3] = { 0.1, 0.01, 0.001 };

    struct Point
    {
        qint64 t = 0;                  // seconds since the epoch, on the log's clock
        LevelsScopeLog::Sample s;
        double inPeak = 0.0;           // measured, else authored (L1)
        double inAverage = 0.0;        // measured PQ average, else authored
        bool computed = false;         // the engine placed this sample
        double outPeak = 0.0, outAverage = 0.0;
        double outProbe[3] = {};
        double kneeNits = 0.0, exposure = 1.0;
    };

    struct Section
    {
        QString id;                    // stable across reloads: start, kind and range
        QString kind;                  // the name it gets by default
        qint64 a = 0, b = 0;           // first and last sample
        bool dolbyVision = false;
        bool rangeKnown = false;
        double black = 0.0, peak = 0.0;
        QString build, files;
        std::vector<Point> points;
    };

    struct Library
    {
        std::vector<Section> sections; // in time order
        QStringList notes;             // per file: what was read, or why not
        bool measured = false;         // any log had detection lines
    };

    // vp.log-family files; a .gz is left out when its plain twin is there too,
    // and a lone .gz is reported (unpack it first).
    inline QStringList LogFiles(const QStringList& paths, QStringList& notes)
    {
        static const QRegularExpression family(QStringLiteral("(^vp.*\\.log)|(\\.log(\\.\\d+)?(\\.gz)?$)"),
            QRegularExpression::CaseInsensitiveOption);
        QSet<QString> names;
        for (const QString& path : paths) names.insert(QFileInfo(path).fileName().toLower());
        QStringList result;
        for (const QString& path : paths)
        {
            const QString name = QFileInfo(path).fileName();
            if (!family.match(name).hasMatch()) continue;
            if (name.endsWith(QStringLiteral(".gz"), Qt::CaseInsensitive))
            {
                if (!names.contains(name.left(name.size() - 3).toLower()))
                    notes << name + QStringLiteral(": compressed; unpack it first");
                continue;
            }
            result << path;
        }
        return result;
    }

    inline QString KindOf(const Section& s)
    {
        if (s.rangeKnown && std::abs(s.black - 0.0501) < 1e-3) return QStringLiteral("Apple TV home screen");
        if (s.dolbyVision && s.b - s.a < 360) return QStringLiteral("Short DV clip");
        return s.dolbyVision ? QStringLiteral("DV programme") : QStringLiteral("HDR10");
    }

    inline Library Read(const QStringList& paths)
    {
        Library library;
        struct Entry { Point point; QString file, build; };
        std::map<QString, Entry> merged;   // one per time, kind, peak and L1 peak
        for (const QString& path : LogFiles(paths, library.notes))
        {
            const QString name = QFileInfo(path).fileName();
            std::ifstream file(path.toStdWString(), std::ios::binary);
            if (!file)
            {
                library.notes << name + QStringLiteral(": could not be read");
                continue;
            }
            LevelsScopeLog::Reader reader;
            for (std::string line; std::getline(file, line);)
            {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                reader.Line(line);
            }
            library.measured = library.measured || reader.Measured();
            const QString build = QString::fromStdString(reader.Build());
            int kept = 0;
            for (const LevelsScopeLog::Sample& sample : reader.Samples())
            {
                const QDateTime when = QDateTime::fromString(QString::fromStdString(sample.time),
                    QStringLiteral("yyyy-MM-dd HH:mm:ss"));
                if (!when.isValid()) continue;
                Entry entry;
                entry.point.t = when.toSecsSinceEpoch();
                entry.point.s = sample;
                entry.point.inPeak = sample.measured && sample.detectedPeakNits > 0 ? sample.detectedPeakNits : sample.authoredPeakNits;
                entry.point.inAverage = sample.measured && sample.detectedPqAverageNits > 0 ?
                    sample.detectedPqAverageNits : sample.authoredAverageNits;
                entry.file = name;
                entry.build = build;
                const QString key = QStringLiteral("%1|%2|%3|%4").arg(entry.point.t).arg(sample.dolbyVision)
                    .arg(sample.detectedPeakNits).arg(sample.authoredPeakNits);
                if (merged.emplace(key, entry).second) ++kept;
            }
            library.notes << QStringLiteral("%1: %2 samples (%3), build %4").arg(name).arg(kept)
                .arg(reader.Measured() ? QStringLiteral("measured and L1") : QStringLiteral("L1 only"))
                .arg(build.isEmpty() ? QStringLiteral("not identified") : build);
        }
        std::vector<Entry> all;
        for (auto& item : merged) all.push_back(item.second);
        std::stable_sort(all.begin(), all.end(), [](const Entry& p, const Entry& q) { return p.point.t < q.point.t; });

        Section current;
        bool open = false;
        QSet<QString> builds, files;
        const auto close = [&] {
            if (open && current.b - current.a >= 120)
            {
                current.kind = KindOf(current);
                current.id = QStringLiteral("%1|%2|%3|%4").arg(current.a).arg(current.dolbyVision)
                    .arg(current.black).arg(current.peak);
                current.build = QStringList(builds.values()).join(QStringLiteral(", "));
                current.files = QStringList(files.values()).join(QStringLiteral(", "));
                library.sections.push_back(current);
            }
            open = false;
            builds.clear();
            files.clear();
        };
        for (const Entry& entry : all)
        {
            const LevelsScopeLog::Sample& s = entry.point.s;
            const bool same = open && s.dolbyVision == current.dolbyVision && s.rangeKnown == current.rangeKnown &&
                s.masteringBlackNits == current.black && s.masteringPeakNits == current.peak &&
                entry.point.t - current.b <= 30;
            if (!same)
            {
                close();
                current = Section{};
                current.a = entry.point.t;
                current.dolbyVision = s.dolbyVision;
                current.rangeKnown = s.rangeKnown;
                current.black = s.masteringBlackNits;
                current.peak = s.masteringPeakNits;
                open = true;
            }
            current.b = entry.point.t;
            current.points.push_back(entry.point);
            if (!entry.build.isEmpty()) builds.insert(entry.build);
            files.insert(entry.file);
        }
        close();
        return library;
    }

    // Where `profile` puts each sample's levels, through the shared engine.
    // False with `error` when libplacebo cannot be loaded.
    inline bool Place(Library& library, const ToneCurveProvider::Request& profile, std::wstring& error)
    {
        for (Section& section : library.sections)
            for (Point& point : section.points)
            {
                point.computed = false;
                if (!(point.inPeak > 0.0)) continue;   // a black frame: nothing to place
                const std::vector<double> probes = { Probes[0], Probes[1], Probes[2], point.inPeak,
                    point.inAverage > 0.0 ? point.inAverage : point.inPeak };
                LevelsScopeEngine::Outcome outcome;
                if (!LevelsScopeEngine::Evaluate(profile, point.s, probes, outcome, error))
                    return false;
                for (int i = 0; i < 3; ++i) point.outProbe[i] = outcome.profileNits[i];
                point.outPeak = outcome.profileNits[3];
                point.outAverage = outcome.profileNits[4];
                const auto& f = ToneCurveProvider::Registered();
                point.kneeNits = f.pqToNits(outcome.result.kneePq);
                point.exposure = outcome.result.exposure;
                point.computed = true;
            }
        return true;
    }
}
