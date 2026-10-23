#pragma once
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSaveFile>
#include <QString>

// The scope's settings, as JSON files in one folder inside the VideoProcessor
// folder: window.json (this PC's view, traces, last folder, config and profile)
// and sections.json (the picker's ticks and names). Never the registry. With no
// folder (a snapshot run) settings live in memory only and nothing is written.
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
}
