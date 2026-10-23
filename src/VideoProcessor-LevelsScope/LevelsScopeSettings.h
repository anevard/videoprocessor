#pragma once
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSaveFile>
#include <QSettings>
#include <QString>

// The scope's settings, as JSON files in one folder inside the VideoProcessor
// folder: window.json (this PC's view, traces, last folder, config and profile)
// and sections.json (the picker's ticks and names). With no folder (a snapshot
// run) settings live in memory only and nothing is written. The one exception
// is the rig the config comes from (Rig, below): in the registry, as the Config
// GUI keeps its own.
namespace LevelsScopeSettings
{
    // <VideoProcessor folder>\levels-scope, beside config\ where the scope runs.
    inline QString DefaultFolder()
    {
        return QDir::cleanPath(QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../levels-scope")));
    }

    // A missing or unreadable file reads as empty.
    inline QJsonObject ReadJson(const QString& path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return {};
        return QJsonDocument::fromJson(file.readAll()).object();
    }

    // Written whole or not at all, so a crash never leaves half a file.
    inline bool WriteJson(const QString& path, const QJsonObject& object)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return false;
        file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
        return file.commit();
    }

    // window.json, saved on every change.
    class Window
    {
    public:
        explicit Window(const QString& folder)
            : path_(folder.isEmpty() ? QString() : QDir(folder).absoluteFilePath(QStringLiteral("window.json")))
        {
            if (!path_.isEmpty()) data_ = ReadJson(path_);
        }

        // 0 Input scope, 1 In vs out.
        int view() const { return data_.value(QStringLiteral("view")).toInt(1); }
        void setView(int view) { set(QStringLiteral("view"), view); }

        bool shown(const QString& trace) const { return data_.value(QStringLiteral("shown")).toObject().value(trace).toBool(true); }
        void setShown(const QString& trace, bool on)
        {
            QJsonObject shown = data_.value(QStringLiteral("shown")).toObject();
            shown[trace] = on;
            set(QStringLiteral("shown"), shown);
        }

        QString lastDir() const { return data_.value(QStringLiteral("lastDir")).toString(); }
        void setLastDir(const QString& dir) { set(QStringLiteral("lastDir"), dir); }

        QString config() const { return data_.value(QStringLiteral("config")).toString(); }
        void setConfig(const QString& path) { set(QStringLiteral("config"), path); }

        QString profile() const { return data_.value(QStringLiteral("profile")).toString(); }
        void setProfile(const QString& section) { set(QStringLiteral("profile"), section); }

    private:
        void set(const QString& key, const QJsonValue& value)
        {
            data_[key] = value;
            if (!path_.isEmpty()) WriteJson(path_, data_);
        }

        QString path_;
        QJsonObject data_;
    };

    // The rig the config comes from, under the scope's own key and laid out
    // like the Config GUI's configRpc values (never his key). selectedInstanceId:
    // "local" for a config file on this PC, "manual:<host>:<port>" for a typed
    // address, else the rig's instance id.
    class Rig
    {
    public:
        QString selectedInstanceId() const { return settings_.value(QStringLiteral("configRpc/selectedInstanceId")).toString(); }
        QString selectedLabel() const { return settings_.value(QStringLiteral("configRpc/selectedLabel")).toString(); }
        QString manualHost() const { return settings_.value(QStringLiteral("configRpc/manualHost")).toString(); }
        quint16 manualPort() const { return static_cast<quint16>(settings_.value(QStringLiteral("configRpc/manualPort"), 41686).toUInt()); }

        void rememberLocal()
        {
            settings_.setValue(QStringLiteral("configRpc/selectedInstanceId"), QStringLiteral("local"));
            settings_.setValue(QStringLiteral("configRpc/selectedLabel"), QStringLiteral("This computer"));
        }
        // As the Config GUI remembers a rig: by instance id, or a typed address.
        void remember(const QString& instanceId, const QString& label, const QString& host, quint16 port)
        {
            settings_.setValue(QStringLiteral("configRpc/selectedLabel"), label);
            if (!instanceId.isEmpty())
                settings_.setValue(QStringLiteral("configRpc/selectedInstanceId"), instanceId);
            else
            {
                settings_.setValue(QStringLiteral("configRpc/selectedInstanceId"), QStringLiteral("manual:%1:%2").arg(host).arg(port));
                settings_.setValue(QStringLiteral("configRpc/manualHost"), host);
                settings_.setValue(QStringLiteral("configRpc/manualPort"), port);
            }
        }

    private:
        QSettings settings_{ QStringLiteral("VideoProcessor"), QStringLiteral("Levels Scope") };
    };
}
