#pragma once
// His unpacking, compiled unchanged: r210 (ReadBigEndian32) and R12B
// (ConvertBlockScalar, eight pixels per 36 bytes).
#include <video_frame_formatter/CR210toRGB48VideoFrameFormatter.Common.h>
#include <video_frame_formatter/CR12BtoRGB48VideoFrameFormatter.Common.h>
#include <QByteArray>
#include <QImage>
#include <QJsonObject>
#include <QString>
#include <algorithm>
#include <cstdint>

// A picking preview of a source capture, made on this PC (VIEWING-PLAN
// CAPTURE-FORMAT: the rig does nothing for it). It shows the signal's code
// values as they are, with no tone mapping: an HDR frame looks flat and dim,
// as it would in any viewer that does not know the transfer. A Dolby Vision
// tunnel frame and the 4:2:2 YCbCr encodings show grey (their intensity or
// luma sample alone).
namespace LevelsScopeThumbnail
{
    constexpr int Width = 320;

    // The short code inside VP's encoding name ("RGB 10-bit (r210)" -> r210;
    // "ARGB 8-bit" -> ARGB), as VP's ToString(VideoFrameEncoding) writes it.
    inline QString Code(const QString& name)
    {
        const int open = name.lastIndexOf(u'('), close = name.lastIndexOf(u')');
        if (open >= 0 && close > open) return name.mid(open + 1, close - open - 1);
        return name.section(u' ', 0, 0);
    }

    // How each encoding code is drawn; empty kind = no preview for it.
    inline QString Kind(const QString& encoding, bool dolbyVisionTunnel)
    {
        if (dolbyVisionTunnel) return QStringLiteral("grey (Dolby Vision intensity)");
        if (encoding == QStringLiteral("r210") || encoding == QStringLiteral("R12B") ||
            encoding == QStringLiteral("ARGB") || encoding == QStringLiteral("BGRA"))
            return QStringLiteral("colour");
        if (encoding == QStringLiteral("v210") || encoding == QStringLiteral("UYVY") ||
            encoding == QStringLiteral("HDYC"))
            return QStringLiteral("grey (luma)");
        return {};
    }

    // The 8-bit intensity of a tunnel word, aligned as his tunnel formatter
    // detects it: an 8-bit sample sits at the top (left) or bottom (right) of
    // each 10-bit field.
    inline bool TunnelLeftAligned(const uint8_t* data, size_t words)
    {
        size_t high8 = 0, low8 = 0;
        const size_t count = (std::min)(words, size_t(4096));
        for (size_t i = 0; i < count; ++i)
        {
            const uint32_t word = ReadBigEndian32(data + i * 4);
            const uint32_t r = (word >> 20) & 0x3FFU, g = (word >> 10) & 0x3FFU, b = word & 0x3FFU;
            high8 += ((r | g | b) & 3U) == 0;
            low8 += ((r | g | b) & 0x300U) == 0;
        }
        return high8 > low8;
    }

    inline QImage Make(const QByteArray& pixels, const QJsonObject& sidecar)
    {
        const QJsonObject frame = sidecar.value(QStringLiteral("frame")).toObject();
        const QJsonObject device = sidecar.value(QStringLiteral("device_state")).toObject();
        const QJsonObject vp = sidecar.value(QStringLiteral("vp_state")).toObject();
        const int width = frame.value(QStringLiteral("width")).toInt();
        const int height = frame.value(QStringLiteral("height")).toInt();
        const int rowBytes = frame.value(QStringLiteral("row_bytes")).toInt();
        const QString encoding = Code(device.value(QStringLiteral("encoding")).toString());
        const bool tunnel = vp.value(QStringLiteral("dolby_vision_tunnel")).toBool();
        const QString kind = Kind(encoding, tunnel);
        if (kind.isEmpty() || width <= 0 || height <= 0 || rowBytes <= 0 ||
            static_cast<qint64>(rowBytes) * height > pixels.size())
            return {};
        const int outWidth = (std::min)(Width, width);
        const int outHeight = (std::max)(1, static_cast<int>(static_cast<qint64>(height) * outWidth / width));
        QImage image(outWidth, outHeight, QImage::Format_RGB888);
        image.fill(Qt::black);
        const auto* data = reinterpret_cast<const uint8_t*>(pixels.constData());
        const bool left = tunnel && TunnelLeftAligned(data, pixels.size() / 4);
        for (int oy = 0; oy < outHeight; ++oy)
        {
            const int y = static_cast<int>(static_cast<qint64>(oy) * height / outHeight);
            const uint8_t* row = data + static_cast<size_t>(y) * rowBytes;
            uchar* out = image.scanLine(oy);
            for (int ox = 0; ox < outWidth; ++ox)
            {
                const int x = static_cast<int>(static_cast<qint64>(ox) * width / outWidth);
                uint8_t r = 0, g = 0, b = 0;
                if (tunnel)
                {
                    // His tunnel unpack puts the green field's 8-bit sample in
                    // the top byte of the 12-bit intensity.
                    const uint32_t word = ReadBigEndian32(row + static_cast<size_t>(x) * 4);
                    r = g = b = static_cast<uint8_t>((((word >> 10) & 0x3FFU) >> (left ? 2 : 0)) & 0xFFU);
                }
                else if (encoding == QStringLiteral("r210"))
                {
                    const uint32_t word = ReadBigEndian32(row + static_cast<size_t>(x) * 4);
                    r = static_cast<uint8_t>(((word >> 20) & 0x3FFU) >> 2);
                    g = static_cast<uint8_t>(((word >> 10) & 0x3FFU) >> 2);
                    b = static_cast<uint8_t>((word & 0x3FFU) >> 2);
                }
                else if (encoding == QStringLiteral("R12B"))
                {
                    // A last, partial block would read past the row.
                    if ((x / static_cast<int>(PIXELS_PER_BLOCK) + 1) * static_cast<int>(BYTES_PER_BLOCK) > rowBytes) continue;
                    uint16_t block[PIXELS_PER_BLOCK * 3];
                    ConvertBlockScalar(row + static_cast<size_t>(x / PIXELS_PER_BLOCK) * BYTES_PER_BLOCK, block);
                    const uint16_t* p = block + (x % PIXELS_PER_BLOCK) * 3;
                    r = static_cast<uint8_t>(p[0] >> 8);
                    g = static_cast<uint8_t>(p[1] >> 8);
                    b = static_cast<uint8_t>(p[2] >> 8);
                }
                else if (encoding == QStringLiteral("ARGB") || encoding == QStringLiteral("BGRA"))
                {
                    // His ARGB formatter: [A R G B]; BGRA: [B G R A].
                    const uint8_t* p = row + static_cast<size_t>(x) * 4;
                    const bool argb = encoding == QStringLiteral("ARGB");
                    r = argb ? p[1] : p[2];
                    g = argb ? p[2] : p[1];
                    b = argb ? p[3] : p[0];
                }
                else if (encoding == QStringLiteral("v210"))
                {
                    // Six pixels in four little-endian words; luma at
                    // w0[10..19] w1[0..9] w1[20..29] w2[10..19] w3[0..9] w3[20..29].
                    const uint8_t* group = row + static_cast<size_t>(x / 6) * 16;
                    static const int word[6] = { 0, 1, 1, 2, 3, 3 };
                    static const int shift[6] = { 10, 0, 20, 10, 0, 20 };
                    const int i = x % 6;
                    const uint8_t* w = group + word[i] * 4;
                    const uint32_t value = static_cast<uint32_t>(w[0]) | (static_cast<uint32_t>(w[1]) << 8) |
                        (static_cast<uint32_t>(w[2]) << 16) | (static_cast<uint32_t>(w[3]) << 24);
                    r = g = b = static_cast<uint8_t>(((value >> shift[i]) & 0x3FFU) >> 2);
                }
                else
                {
                    // UYVY and HDYC: [U Y V Y], luma in the odd bytes.
                    r = g = b = row[static_cast<size_t>(x) * 2 + 1];
                }
                out[ox * 3] = r;
                out[ox * 3 + 1] = g;
                out[ox * 3 + 2] = b;
            }
        }
        return image;
    }
}
