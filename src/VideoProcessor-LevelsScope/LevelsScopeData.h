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
#include <cmath>
#include <deque>
#include <fstream>
#include <map>
#include <vector>

// The Levels Scope's data: samples on one clock, cut into sections, each
// sample carrying where a profile put its levels.
//
// One Builder takes samples in time order and extends or opens sections, so
// a log read and a live source (VP's configuration RPC, later) build the
// same way. Sections live in a deque, which never moves them, because the
// chart holds pointers to them while samples keep arriving. Times are
// seconds with millisecond resolution: older logs give whole seconds, newer
// ones milliseconds (time=), live samples arrive several times a second.
//
// The cut rules, as the web page had them: a section is a run of one declared
// source range (DV or not, mastering black and peak) without a gap over 30 s;
// from a log, a section under 2 minutes is dropped.
namespace LevelsScopeData
{
    // The fixed probe levels of the In vs out view, in nits.
    constexpr double Probes[3] = { 0.1, 0.01, 0.001 };

    struct Point
    {
        double t = 0.0;                // seconds since the epoch, millisecond resolution
        LevelsScopeLog::Sample s;
        double inPeak = 0.0;           // measured, else authored (L1)
        double inAverage = 0.0;        // measured PQ average, else authored
        // The profile this sample is placed with: empty for the one chosen
        // on the page (logs); the profile VP reported as active (live).
        QString profile;
        bool computed = false;         // the engine placed this sample
        double outPeak = 0.0, outAverage = 0.0;
        double outProbe[3] = {};
        double kneeNits = 0.0, exposure = 1.0;
    };

    // Where a setting changed: the DTM profile, a render option or the target.
    struct Mark
    {
        double t = 0.0;                // seconds since the epoch
        QString text;
    };

    struct Section
    {
        QString id;                    // stable across reloads: start, kind and range
        QString kind;                  // the name it gets by default
        double a = 0.0, b = 0.0;       // first and last sample, seconds
        bool dolbyVision = false;
        bool rangeKnown = false;
        double black = 0.0, peak = 0.0;
        QString build, files;
        bool open = false;             // still receiving samples
        std::vector<Point> points;
        std::vector<Mark> marks;       // in time order, inside or just before the section
    };

    // A Point from a logged or live sample; `t` in seconds.
    inline Point PointOf(const LevelsScopeLog::Sample& s, double t)
    {
        Point point;
        point.t = t;
        point.s = s;
        point.inPeak = s.measured && s.detectedPeakNits > 0 ? s.detectedPeakNits : s.authoredPeakNits;
        point.inAverage = s.measured && s.detectedPqAverageNits > 0 ? s.detectedPqAverageNits : s.authoredAverageNits;
        return point;
    }

    inline QString KindOf(const Section& s)
    {
        if (s.rangeKnown && std::abs(s.black - 0.0501) < 1e-3) return QStringLiteral("Apple TV home screen");
        if (s.dolbyVision && s.b - s.a < 360) return QStringLiteral("Short DV clip");
        return s.dolbyVision ? QStringLiteral("DV programme") : QStringLiteral("HDR10");
    }

    class Builder
    {
    public:
        // `keepShort`: keep sections under 2 minutes (live keeps everything).
        explicit Builder(std::deque<Section>& sections, bool keepShort)
            : sections_(sections), keepShort_(keepShort) {}

        // Samples in time order. Returns the section the point went into.
        Section& Add(const Point& point, const QString& build = QString(), const QString& file = QString())
        {
            const LevelsScopeLog::Sample& s = point.s;
            Section* current = open_ ? &sections_.back() : nullptr;
            const bool same = current && s.dolbyVision == current->dolbyVision && s.rangeKnown == current->rangeKnown &&
                s.masteringBlackNits == current->black && s.masteringPeakNits == current->peak &&
                point.t - current->b <= 30.0;
            if (!same)
            {
                Close();
                Section next;
                next.a = point.t;
                next.dolbyVision = s.dolbyVision;
                next.rangeKnown = s.rangeKnown;
                next.black = s.masteringBlackNits;
                next.peak = s.masteringPeakNits;
                next.open = true;
                // Logs key sections by whole seconds even with millisecond
                // times, so names saved from older reads still match.
                const double idT = keepShort_ ? point.t : std::floor(point.t);
                next.id = QStringLiteral("%1|%2|%3|%4")
                    .arg(QString::number(idT, 'f', idT == std::floor(idT) ? 0 : 3)).arg(s.dolbyVision)
                    .arg(s.masteringBlackNits).arg(s.masteringPeakNits);
                sections_.push_back(next);
                open_ = true;
                current = &sections_.back();
            }
            current->b = point.t;
            current->kind = KindOf(*current);
            current->points.push_back(point);
            if (!build.isEmpty() && !current->build.split(QStringLiteral(", ")).contains(build))
                current->build += (current->build.isEmpty() ? QString() : QStringLiteral(", ")) + build;
            if (!file.isEmpty() && !current->files.split(QStringLiteral(", ")).contains(file))
                current->files += (current->files.isEmpty() ? QString() : QStringLiteral(", ")) + file;
            return *current;
        }

        // Ends the open section: a short one is dropped unless kept.
        void Close()
        {
            if (!open_) return;
            open_ = false;
            Section& last = sections_.back();
            last.open = false;
            if (!keepShort_ && last.b - last.a < 120.0)
                sections_.pop_back();
        }

    private:
        std::deque<Section>& sections_;
        bool keepShort_;
        bool open_ = false;
    };

    struct Library
    {
        std::deque<Section> sections;  // in time order; never moved
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

    // Logs: every file's samples merged (a sample logged twice counts once),
    // put in time order, then built into sections.
    inline Library Read(const QStringList& paths)
    {
        Library library;
        struct Entry { Point point; QString file, build; };
        std::map<QString, Entry> merged;   // one per time (ms), kind, peak and L1 peak
        std::map<QString, Mark> marks;     // one per time and text, in time order
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
                const QString stamp = QString::fromStdString(sample.time);
                QDateTime when = QDateTime::fromString(stamp, QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
                if (!when.isValid())
                    when = QDateTime::fromString(stamp, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
                if (!when.isValid()) continue;
                Entry entry{ PointOf(sample, when.toMSecsSinceEpoch() / 1000.0), name, build };
                const QString key = QStringLiteral("%1|%2|%3|%4").arg(when.toMSecsSinceEpoch()).arg(sample.dolbyVision)
                    .arg(sample.detectedPeakNits).arg(sample.authoredPeakNits);
                if (merged.emplace(key, entry).second) ++kept;
            }
            for (const LevelsScopeLog::Marker& marker : reader.Markers())
            {
                const QDateTime when = QDateTime::fromString(QString::fromStdString(marker.time),
                    QStringLiteral("yyyy-MM-dd HH:mm:ss"));
                if (!when.isValid()) continue;
                const QString text = QString::fromStdString(marker.text);
                marks.emplace(QStringLiteral("%1|%2").arg(when.toMSecsSinceEpoch(), 15, 10, QChar(u'0')).arg(text),
                    Mark{ when.toMSecsSinceEpoch() / 1000.0, text });
            }
            library.notes << QStringLiteral("%1: %2 samples (%3), build %4").arg(name).arg(kept)
                .arg(reader.Measured() ? QStringLiteral("measured and L1") : QStringLiteral("L1 only"))
                .arg(build.isEmpty() ? QStringLiteral("not identified") : build);
        }
        std::vector<const Entry*> ordered;
        for (const auto& item : merged) ordered.push_back(&item.second);
        std::stable_sort(ordered.begin(), ordered.end(),
            [](const Entry* p, const Entry* q) { return p->point.t < q->point.t; });
        Builder builder(library.sections, false);
        for (const Entry* entry : ordered)
            builder.Add(entry->point, entry->build, entry->file);
        builder.Close();
        // Each mark goes to the section it falls in, or one starting within
        // 30 s after it (a profile is logged as the renderer starts, before
        // the first sample); it is drawn no earlier than the section starts.
        for (const auto& item : marks)
            for (Section& section : library.sections)
                if (item.second.t >= section.a - 30.0 && item.second.t <= section.b)
                {
                    section.marks.push_back(item.second);
                    section.marks.back().t = (std::max)(item.second.t, section.a);
                    break;
                }
        return library;
    }

    // Where `profile` puts one sample, through the shared engine. False with
    // `error` when libplacebo cannot be loaded.
    inline bool PlacePoint(Point& point, const ToneCurveProvider::Request& profile, std::wstring& error)
    {
        point.computed = false;
        if (!(point.inPeak > 0.0)) return true;   // a black frame: nothing to place
        const std::vector<double> probes = { Probes[0], Probes[1], Probes[2], point.inPeak,
            point.inAverage > 0.0 ? point.inAverage : point.inPeak };
        LevelsScopeEngine::Outcome outcome;
        if (!LevelsScopeEngine::Evaluate(profile, point.s, probes, outcome, error))
            return false;
        for (int i = 0; i < 3; ++i) point.outProbe[i] = outcome.profileNits[i];
        point.outPeak = outcome.profileNits[3];
        point.outAverage = outcome.profileNits[4];
        point.kneeNits = ToneCurveProvider::Registered().pqToNits(outcome.result.kneePq);
        point.exposure = outcome.result.exposure;
        point.computed = true;
        return true;
    }

    // Every sample of `library` placed with `profileFor(point.profile)`.
    template<class ProfileFor>
    bool Place(Library& library, const ProfileFor& profileFor, std::wstring& error)
    {
        for (Section& section : library.sections)
            for (Point& point : section.points)
                if (!PlacePoint(point, profileFor(point.profile), error))
                    return false;
        return true;
    }
}
