#pragma once
#include "VpLog.h"
#include <QDateTime>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <algorithm>
#include <atomic>
#include <fstream>
#include <map>
#include <thread>
#include <utility>
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

    // One log file read on its own: its samples and markers in file order,
    // each under the key Merge counts it by.
    struct FileLog
    {
        QString name;           // the file's name, as the notes give it
        bool readable = false;
        bool measured = false;  // the file had detection lines
        QString build;
        std::vector<std::pair<QString, LoggedSample>> samples;  // key: time (ms), kind, peak and L1 peak
        std::vector<std::pair<QString, LoggedMarker>> markers;  // key: time and text
    };

    inline FileLog ReadFile(const QString& path)
    {
        FileLog log;
        log.name = QFileInfo(path).fileName();
        std::ifstream file(path.toStdWString(), std::ios::binary);
        if (!file) return log;
        log.readable = true;
        VpLog::Reader reader;
        reader.Read(file);
        log.measured = reader.Measured();
        log.build = QString::fromStdString(reader.Build());
        for (const VpLog::Sample& sample : reader.Samples())
        {
            const QDateTime when = When(sample.time);
            if (!when.isValid()) continue;
            const QString key = QStringLiteral("%1|%2|%3|%4").arg(when.toMSecsSinceEpoch()).arg(sample.dolbyVision)
                .arg(sample.detectedPeakNits).arg(sample.authoredPeakNits);
            log.samples.emplace_back(key, LoggedSample{ when.toMSecsSinceEpoch() / 1000.0, sample, log.name, log.build });
        }
        for (const VpLog::Marker& marker : reader.Markers())
        {
            const QDateTime when = When(marker.time);
            if (!when.isValid()) continue;
            const QString text = QString::fromStdString(marker.text);
            log.markers.emplace_back(QStringLiteral("%1|%2").arg(when.toMSecsSinceEpoch(), 15, 10, QChar(u'0')).arg(text),
                LoggedMarker{ when.toMSecsSinceEpoch() / 1000.0, text });
        }
        return log;
    }

    // ReadFile for each path, several files at once; in the order given.
    inline std::vector<FileLog> ReadFiles(const QStringList& paths)
    {
        const int count = static_cast<int>(paths.size());
        std::vector<FileLog> files(count);
        std::atomic<int> next{ 0 };
        const auto work = [&] {
            for (int i = next++; i < count; i = next++)
                files[i] = ReadFile(paths.at(i));
        };
        const int threads = (std::min)(count, static_cast<int>((std::max)(1u, std::thread::hardware_concurrency())));
        std::vector<std::thread> pool;
        for (int i = 1; i < threads; ++i) pool.emplace_back(work);
        work();
        for (std::thread& thread : pool) thread.join();
        return files;
    }

    // Files from ReadFile merged in the order given, after `notes` (from
    // LogFiles); a file's note counts the samples no earlier file had.
    inline Logs Merge(const std::vector<const FileLog*>& files, const QStringList& notes)
    {
        Logs logs;
        logs.notes = notes;
        std::map<QString, LoggedSample> merged;   // one per time (ms), kind, peak and L1 peak
        std::map<QString, LoggedMarker> markers;  // one per time and text, in time order
        for (const FileLog* file : files)
        {
            if (!file->readable)
            {
                logs.notes << file->name + QStringLiteral(": could not be read");
                continue;
            }
            logs.measured = logs.measured || file->measured;
            int kept = 0;
            for (const auto& sample : file->samples)
                if (merged.emplace(sample.first, sample.second).second) ++kept;
            for (const auto& marker : file->markers)
                markers.emplace(marker.first, marker.second);
            logs.notes << QStringLiteral("%1: %2 samples (%3), build %4").arg(file->name).arg(kept)
                .arg(file->measured ? QStringLiteral("measured and L1") : QStringLiteral("L1 only"))
                .arg(file->build.isEmpty() ? QStringLiteral("not identified") : file->build);
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
