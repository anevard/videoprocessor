#pragma once
#include <ConfigEditorCore.h>
#include <ConfigurationRpcClient.h>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <string>
#include <vector>

// Where the scope's config and logs come from: a config's DTM profiles, a
// rig's config over the network, and the log files to open.
namespace LevelsScopeSources
{
    // Rendering profiles that set any tone or target key: the DTM profiles.
    inline bool IsDtmProfile(const ConfigEditorCore::ConfigDocument& doc, const std::string& section)
    {
        for (const char* key : { "tone_mapping", "peak_detection", "sdr_target_nits", "dynamic_target", "knee_default" })
            if (!doc.Get(section.c_str(), key).empty()) return true;
        return false;
    }

    // One DTM profile: its config section, and the name the picker shows.
    struct DtmProfile
    {
        QString shown;
        QString name;
    };

    // A config's DTM profiles, in the order the file has them.
    inline std::vector<DtmProfile> DtmProfiles(const ConfigEditorCore::ConfigDocument& document)
    {
        std::vector<DtmProfile> result;
        for (const std::string& section : document.SectionNames())
        {
            const QString name = QString::fromStdString(section);
            if (!name.startsWith(QStringLiteral("vprenderer"), Qt::CaseInsensitive) || !IsDtmProfile(document, section)) continue;
            const QString label = QString::fromLocal8Bit(document.Get(section.c_str(), "label").c_str()).trimmed();
            const QString shown = !label.isEmpty() ? label :
                name.contains(u'.') ? name.mid(name.indexOf(u'.') + 1) : name;
            result.push_back({ shown, name });
        }
        return result;
    }

    // A rig's config as the Config GUI fetches it: GetConfig over his
    // ConfigurationRpcClient. Blocks on the network; run it off the UI thread.
    struct RigConfig
    {
        std::string path;
        std::string bytes;
        std::string error;
        bool ok = false;
    };

    inline void FetchRigConfig(const QString& host, quint16 port, RigConfig& result)
    {
        ConfigurationRpcClient client(host.toStdString(), port);
        result.ok = client.GetConfig(result.path, result.bytes, result.error);
    }

    // The fetched bytes as a document, loaded as the Config GUI loads them.
    inline ConfigEditorCore::ConfigDocument RigDocument(const RigConfig& config)
    {
        ConfigEditorCore::ConfigDocument document;
        document.LoadBytes(QString::fromUtf8(config.path).toStdWString(), config.bytes);
        return document;
    }

    // Every file in a folder and its subfolders.
    inline QStringList FilesIn(const QString& dir)
    {
        QStringList files;
        QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) files << it.next();
        return files;
    }

    // Opened logs join those already open (a sample in two files counts once).
    inline void AddLogs(QStringList& files, const QStringList& paths)
    {
        for (const QString& path : paths)
        {
            const QString file = QFileInfo(path).absoluteFilePath();
            if (!files.contains(file, Qt::CaseInsensitive)) files << file;
        }
    }
}
