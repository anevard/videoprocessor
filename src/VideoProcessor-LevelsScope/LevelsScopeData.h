#pragma once
#include <LoggedScene.h>
#include <VpLog.h>
#include <VpLogFiles.h>
#include <QString>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <deque>
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
        VpLog::Sample s;
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
    inline Point PointOf(const VpLog::Sample& s, double t)
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
            const VpLog::Sample& s = point.s;
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

    // Logs merged and in time order (VpLogFiles::Merge), built into sections.
    inline Library FromLogs(const VpLogFiles::Logs& logs)
    {
        Library library;
        library.notes = logs.notes;
        library.measured = logs.measured;
        Builder builder(library.sections, false);
        for (const VpLogFiles::LoggedSample& logged : logs.samples)
            builder.Add(PointOf(logged.sample, logged.t), logged.build, logged.file);
        builder.Close();
        // Each mark goes to the section it falls in, or one starting within
        // 30 s after it (a profile is logged as the renderer starts, before
        // the first sample); it is drawn no earlier than the section starts.
        for (const VpLogFiles::LoggedMarker& marker : logs.markers)
            for (Section& section : library.sections)
                if (marker.t >= section.a - 30.0 && marker.t <= section.b)
                {
                    section.marks.push_back(Mark{ marker.t, marker.text });
                    section.marks.back().t = (std::max)(marker.t, section.a);
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
        LoggedScene::Outcome outcome;
        if (!LoggedScene::Evaluate(profile, point.s, probes, outcome, error))
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
