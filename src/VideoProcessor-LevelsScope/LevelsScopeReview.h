#pragma once
#include <ConfigurationRpcClient.h>
#include <SourceCaptureProtocol.h>
#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <algorithm>
#include <string>

// Review from the second PC (dtm-tuning REVIEW-BUILD.md): send a capture to the
// rig once and show it in place of the live picture, or go back to live. The
// sidecar decides the still's kind, never the rig's live profile.
namespace LevelsScopeReview
{
    // The still's signal from its sidecar. Normally the raw device state, as the
    // card reported it. An LLDV capture (VP promoted SDR + BT.2020 to PQ, no
    // tunnel) re-enters as PQ with the HDR10 values VP applied.
    inline bool StillFromSidecar(const QJsonObject& root, qint64 fileBytes,
        SourceCaptureProtocol::ReviewStill& still, QString& error)
    {
        const QJsonObject frame = root.value(QStringLiteral("frame")).toObject();
        const QJsonObject device = root.value(QStringLiteral("device_state")).toObject();
        const QJsonObject vp = root.value(QStringLiteral("vp_state")).toObject();
        const qint64 byteSize = frame.value(QStringLiteral("byte_size")).toVariant().toLongLong();
        if (byteSize <= 0 || byteSize != fileBytes)
        {
            error = QStringLiteral("The capture's pixel file does not match its sidecar.");
            return false;
        }
        const bool tunnel = vp.value(QStringLiteral("dolby_vision_tunnel")).toBool();
        const bool lldv = !tunnel &&
            vp.value(QStringLiteral("eotf")).toString() != device.value(QStringLiteral("eotf")).toString();
        const QJsonObject& signal = lldv ? vp : device;
        still.byteSize = static_cast<uint32_t>(byteSize);
        still.width = static_cast<uint32_t>(frame.value(QStringLiteral("width")).toInt());
        still.height = static_cast<uint32_t>(frame.value(QStringLiteral("height")).toInt());
        still.encoding = device.value(QStringLiteral("encoding")).toString().toStdString();
        still.eotf = signal.value(QStringLiteral("eotf")).toString().toStdString();
        still.colorspace = signal.value(QStringLiteral("colorspace")).toString().toStdString();
        still.invertedVertical = device.value(QStringLiteral("inverted_vertical")).toBool();
        still.dolbyVisionTunnel = tunnel;
        const QJsonObject hdr = signal.value(QStringLiteral("hdr10")).toObject();
        still.hasHdr10 = !hdr.isEmpty();
        if (still.hasHdr10)
        {
            const QJsonArray primaries = hdr.value(QStringLiteral("primaries")).toArray();
            const QJsonArray white = hdr.value(QStringLiteral("white_point")).toArray();
            const double values[12] = {
                hdr.value(QStringLiteral("mastering_min_nits")).toDouble(),
                hdr.value(QStringLiteral("mastering_max_nits")).toDouble(),
                hdr.value(QStringLiteral("max_cll")).toDouble(),
                hdr.value(QStringLiteral("max_fall")).toDouble(),
                primaries.at(0).toDouble(), primaries.at(1).toDouble(),
                primaries.at(2).toDouble(), primaries.at(3).toDouble(),
                primaries.at(4).toDouble(), primaries.at(5).toDouble(),
                white.at(0).toDouble(), white.at(1).toDouble() };
            std::copy(std::begin(values), std::end(values), std::begin(still.hdr10));
        }
        if (still.width == 0 || still.height == 0 || still.encoding.empty() ||
            still.eotf.empty() || still.colorspace.empty())
        {
            error = QStringLiteral("The capture's sidecar is missing its signal.");
            return false;
        }
        return true;
    }

    // Blocks on the network; run it off the UI thread.
    inline void Show(const QString& host, quint16 port, const QString& pixelPath,
        const QJsonObject& root, std::string& error)
    {
        QFile file(pixelPath);
        if (!file.open(QIODevice::ReadOnly))
        {
            error = "Could not read the capture's pixel file.";
            return;
        }
        const QByteArray pixels = file.readAll();
        SourceCaptureProtocol::ReviewStill still;
        QString problem;
        if (!StillFromSidecar(root, pixels.size(), still, problem))
        {
            error = problem.toStdString();
            return;
        }
        ConfigurationRpcClient client(host.toStdString(), port);
        uint64_t stillId = 0;
        if (!client.BeginReview(still, stillId, error)) return;
        const auto* bytes = reinterpret_cast<const uint8_t*>(pixels.constData());
        const uint32_t total = still.byteSize;
        for (uint32_t offset = 0; offset < total;)
        {
            const uint32_t length = (std::min)(SourceCaptureProtocol::MaximumPieceBytes, total - offset);
            if (!client.SendReviewPiece(stillId, offset, bytes + offset, length, error)) return;
            offset += length;
        }
        client.ShowReview(stillId, error);
    }

    inline void Live(const QString& host, quint16 port, std::string& error)
    {
        ConfigurationRpcClient client(host.toStdString(), port);
        client.ReviewLive(error);
    }
}
