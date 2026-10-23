#pragma once
#include <ConfigurationRpcClient.h>
#include <SourceCaptureProtocol.h>
#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QThread>
#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

// A source capture from the rig: VP copies the next frame it admits, the scope
// pulls the sidecar and the pixels and saves both in captures\. The rig keeps
// nothing once the pull ends (VIEWING-PLAN TRANSPORT).
namespace LevelsScopeCapture
{
    // <VideoProcessor folder>\captures, beside levels-scope\.
    inline QString Folder()
    {
        return QDir::cleanPath(QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../captures")));
    }

    struct Result
    {
        bool ok = false;
        std::string error;
        QString pixelPath;
        QString sidecarPath;
        int width = 0;
        int height = 0;
        QString encoding;
        bool rendererReport = false;
    };

    // Blocks on the network; run it off the UI thread.
    inline void Capture(const QString& host, quint16 port, Result& result)
    {
        using SourceCaptureProtocol::PullState;
        ConfigurationRpcClient client(host.toStdString(), port);
        uint64_t id = 0;
        if (!client.ArmSourceCapture(id, result.error)) return;
        struct ReleaseOnExit
        {
            const ConfigurationRpcClient& client;
            uint64_t id;
            ~ReleaseOnExit() { std::string ignored; client.ReleaseSourceCapture(id, ignored); }
        } release{ client, id };

        // The sidecar first: it arrives once the frame is copied and the
        // renderer has reported on it (or VP gave up waiting).
        ConfigurationRpcClient::SourceCapturePiece piece;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        for (;;)
        {
            if (!client.PullSourceCapture(id, 0, 0, piece, result.error)) return;
            if (piece.state == PullState::Ready) break;
            if (piece.state == PullState::Failed) { result.error = piece.reason; return; }
            if (std::chrono::steady_clock::now() >= deadline)
            {
                result.error = "VP received no source frame to capture.";
                return;
            }
            QThread::msleep(100);
        }
        const std::string sidecar = piece.sidecar;
        const uint32_t frameBytes = piece.frameBytes;

        QByteArray pixels;
        pixels.reserve(static_cast<int>(frameBytes));
        for (uint32_t offset = 0; offset < frameBytes;)
        {
            const uint32_t length = (std::min)(SourceCaptureProtocol::MaximumPieceBytes, frameBytes - offset);
            if (!client.PullSourceCapture(id, offset, length, piece, result.error)) return;
            if (piece.state == PullState::Failed) { result.error = piece.reason; return; }
            if (piece.state != PullState::Ready || piece.bytes.empty() || piece.frameBytes != frameBytes)
            {
                result.error = "VP returned an unexpected capture piece.";
                return;
            }
            pixels.append(reinterpret_cast<const char*>(piece.bytes.data()), static_cast<int>(piece.bytes.size()));
            offset += static_cast<uint32_t>(piece.bytes.size());
        }
        if (static_cast<uint32_t>(pixels.size()) != frameBytes)
        {
            result.error = "The captured frame arrived incomplete.";
            return;
        }

        QJsonParseError parseError{};
        QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(sidecar), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject())
        {
            result.error = "VP returned an unreadable capture sidecar.";
            return;
        }
        QJsonObject root = document.object();
        QJsonObject frame = root.value(QStringLiteral("frame")).toObject();
        // The hash is taken here, on what arrived, for copies made by hand later.
        frame.insert(QStringLiteral("pixel_sha256"),
            QString::fromLatin1(QCryptographicHash::hash(pixels, QCryptographicHash::Sha256).toHex()));
        root.insert(QStringLiteral("frame"), frame);
        const QJsonObject identity = root.value(QStringLiteral("identity")).toObject();
        const QJsonObject device = root.value(QStringLiteral("device_state")).toObject();
        result.width = frame.value(QStringLiteral("width")).toInt();
        result.height = frame.value(QStringLiteral("height")).toInt();
        result.encoding = device.value(QStringLiteral("encoding")).toString();
        result.rendererReport = root.value(QStringLiteral("renderer")).toObject()
            .value(QStringLiteral("available")).toBool();

        // capture-<log time>-<id>: sorts by time; ':' cannot appear in a name.
        QString stamp = identity.value(QStringLiteral("capture_time")).toString();
        stamp.replace(u':', u'-');
        const QString stem = QStringLiteral("capture-%1-%2").arg(stamp)
            .arg(static_cast<qulonglong>(identity.value(QStringLiteral("capture_id")).toDouble()));
        const QDir folder(Folder());
        if (!folder.mkpath(QStringLiteral(".")))
        {
            result.error = "Could not create the captures folder.";
            return;
        }
        result.pixelPath = folder.absoluteFilePath(stem + QStringLiteral(".raw"));
        result.sidecarPath = folder.absoluteFilePath(stem + QStringLiteral(".json"));
        QFile pixelFile(result.pixelPath);
        QFile sidecarFile(result.sidecarPath);
        if (!pixelFile.open(QIODevice::WriteOnly) || pixelFile.write(pixels) != pixels.size() ||
            !sidecarFile.open(QIODevice::WriteOnly) ||
            sidecarFile.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0)
        {
            result.error = "Could not save the capture in the captures folder.";
            return;
        }
        result.ok = true;
    }
}
