#pragma once
#include "VpLog.h"
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

// vp.log files read together: which of the given files are logs, each one
// through VpLog::Reader, and all their samples and markers merged on one
// clock (a sample logged in two files counts once), in time order.
namespace VpLogFiles
{
    // A logged sample, its time in seconds since the epoch (millisecond
    // resolution), and the file and build it came from.
    struct LoggedSample
    {
        double t = 0.0;
        VpLog::Sample sample;
        QString file, build;
    };

    // Where a setting changed, in seconds since the epoch.
    struct LoggedMarker
    {
        double t = 0.0;
        QString text;
    };

    struct Logs
    {
        std::vector<LoggedSample> samples;  // in time order
        std::vector<LoggedMarker> markers;  // in time order
        QStringList notes;                  // per file: what was read, or why not
        bool measured = false;              // any log had detection lines
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

    // A logged time, "yyyy-MM-dd HH:mm:ss" with ".zzz" when the line had
    // time=; invalid when it is neither.
    inline QDateTime When(const std::string& time)
    {
        const QString stamp = QString::fromStdString(time);
        QDateTime when = QDateTime::fromString(stamp, QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
        if (!when.isValid())
            when = QDateTime::fromString(stamp, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        return when;
    }

    inline Logs Read(const QStringList& paths)
    {
        Logs logs;
        std::map<QString, LoggedSample> merged;   // one per time (ms), kind, peak and L1 peak
        std::map<QString, LoggedMarker> markers;  // one per time and text, in time order
        for (const QString& path : LogFiles(paths, logs.notes))
        {
            const QString name = QFileInfo(path).fileName();
            std::ifstream file(path.toStdWString(), std::ios::binary);
            if (!file)
            {
                logs.notes << name + QStringLiteral(": could not be read");
                continue;
            }
            VpLog::Reader reader;
            reader.Read(file);
            logs.measured = logs.measured || reader.Measured();
            const QString build = QString::fromStdString(reader.Build());
            int kept = 0;
            for (const VpLog::Sample& sample : reader.Samples())
            {
                const QDateTime when = When(sample.time);
                if (!when.isValid()) continue;
                const QString key = QStringLiteral("%1|%2|%3|%4").arg(when.toMSecsSinceEpoch()).arg(sample.dolbyVision)
                    .arg(sample.detectedPeakNits).arg(sample.authoredPeakNits);
                if (merged.emplace(key, LoggedSample{ when.toMSecsSinceEpoch() / 1000.0, sample, name, build }).second) ++kept;
            }
            for (const VpLog::Marker& marker : reader.Markers())
            {
                const QDateTime when = When(marker.time);
                if (!when.isValid()) continue;
                const QString text = QString::fromStdString(marker.text);
                markers.emplace(QStringLiteral("%1|%2").arg(when.toMSecsSinceEpoch(), 15, 10, QChar(u'0')).arg(text),
                    LoggedMarker{ when.toMSecsSinceEpoch() / 1000.0, text });
            }
            logs.notes << QStringLiteral("%1: %2 samples (%3), build %4").arg(name).arg(kept)
                .arg(reader.Measured() ? QStringLiteral("measured and L1") : QStringLiteral("L1 only"))
                .arg(build.isEmpty() ? QStringLiteral("not identified") : build);
        }
        std::vector<const LoggedSample*> ordered;
        for (const auto& item : merged) ordered.push_back(&item.second);
        std::stable_sort(ordered.begin(), ordered.end(),
            [](const LoggedSample* p, const LoggedSample* q) { return p->t < q->t; });
        for (const LoggedSample* sample : ordered) logs.samples.push_back(*sample);
        for (const auto& item : markers) logs.markers.push_back(item.second);
        return logs;
    }
}
