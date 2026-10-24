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
#include <cmath>
#include <cstdint>

// A picking preview of a source capture, made on this PC (VIEWING-PLAN
// CAPTURE-FORMAT: the rig does nothing for it). Colour, simple and NOT
// accurate (operator 2026-10-23): YCbCr goes through the BT.2020 or BT.709
// matrix, and a PQ signal is brought to a viewable level by a fixed curve.
// A Dolby Vision tunnel frame is its I, P and T samples read as Y, Cr and
// -Cb, without Dolby's reshaping.
namespace LevelsScopeThumbnail
{
    constexpr int Width = 320;

    struct Picture
    {
        bool bt2020 = false;
        bool pq = false;
    };

    // PQ code value to a screen level: nits / 200, then gamma 2.2. Fixed, so
    // previews compare; highlights above 200 nits clip.
    inline double PqToScreen(double code)
    {
        constexpr double m1 = 0.1593017578125, m2 = 78.84375;
        constexpr double c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875;
        const double p = std::pow((std::clamp)(code, 0.0, 1.0), 1.0 / m2);
        const double nits = 10000.0 * std::pow((std::max)(p - c1, 0.0) / (c2 - c3 * p), 1.0 / m1);
        return std::pow((std::min)(nits / 200.0, 1.0), 1.0 / 2.2);
    }

    inline uint8_t ToByte(double value, const Picture& picture)
    {
        value = (std::clamp)(value, 0.0, 1.0);
        if (picture.pq) value = PqToScreen(value);
        return static_cast<uint8_t>(value * 255.0 + 0.5);
    }

    // Y in 0..1, Cb and Cr in -0.5..0.5.
    inline void FromYCbCr(double y, double cb, double cr, const Picture& picture, uint8_t& r, uint8_t& g, uint8_t& b)
    {
        const double kr = picture.bt2020 ? 0.2627 : 0.2126, kb = picture.bt2020 ? 0.0593 : 0.0722;
        const double red = y + 2.0 * (1.0 - kr) * cr;
        const double blue = y + 2.0 * (1.0 - kb) * cb;
        const double green = (y - kr * red - kb * blue) / (1.0 - kr - kb);
        r = ToByte(red, picture);
        g = ToByte(green, picture);
        b = ToByte(blue, picture);
    }

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
        if (dolbyVisionTunnel) return QStringLiteral("colour (rough)");
        if (encoding == QStringLiteral("r210") || encoding == QStringLiteral("R12B") ||
            encoding == QStringLiteral("ARGB") || encoding == QStringLiteral("BGRA") ||
            encoding == QStringLiteral("v210") || encoding == QStringLiteral("UYVY") ||
            encoding == QStringLiteral("HDYC"))
            return QStringLiteral("colour");
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
        Picture picture;
        picture.bt2020 = vp.value(QStringLiteral("colorspace")).toString().contains(QStringLiteral("2020"));
        const QString eotf = vp.value(QStringLiteral("eotf")).toString();
        picture.pq = eotf.contains(QStringLiteral("PQ")) || eotf.contains(QStringLiteral("2084"));
        const unsigned shift = left ? 2U : 0U;
        const auto littleEndian = [](const uint8_t* w) {
            return static_cast<uint32_t>(w[0]) | (static_cast<uint32_t>(w[1]) << 8) |
                (static_cast<uint32_t>(w[2]) << 16) | (static_cast<uint32_t>(w[3]) << 24);
        };
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
                    // His tunnel unpack (ConvertRowsScalar): a 12-bit intensity
                    // and a 12-bit chroma per word, the chroma alternating P on
                    // even pixels and T on odd ones.
                    const int even = x & ~1;
                    if (even + 1 >= width) continue;
                    const uint32_t first = ReadBigEndian32(row + static_cast<size_t>(even) * 4);
                    const uint32_t second = ReadBigEndian32(row + static_cast<size_t>(even + 1) * 4);
                    const uint32_t word = x == even ? first : second;
                    const auto intensity = [shift](uint32_t p) { return ((p >> (2 + shift)) & 0xFF00U) | ((p << (4 - shift)) & 0xF0U); };
                    const auto chroma = [shift](uint32_t p) { return ((p >> (12 + shift)) & 0xFF00U) | ((p >> shift) & 0xF0U); };
                    const double pSample = chroma(first) / 65535.0 - 0.5;
                    const double tSample = chroma(second) / 65535.0 - 0.5;
                    FromYCbCr(intensity(word) / 65535.0, -tSample, pSample, picture, r, g, b);
                }
                else if (encoding == QStringLiteral("r210"))
                {
                    const uint32_t word = ReadBigEndian32(row + static_cast<size_t>(x) * 4);
                    r = ToByte(((word >> 20) & 0x3FFU) / 1023.0, picture);
                    g = ToByte(((word >> 10) & 0x3FFU) / 1023.0, picture);
                    b = ToByte((word & 0x3FFU) / 1023.0, picture);
                }
                else if (encoding == QStringLiteral("R12B"))
                {
                    // A last, partial block would read past the row.
                    if ((x / static_cast<int>(PIXELS_PER_BLOCK) + 1) * static_cast<int>(BYTES_PER_BLOCK) > rowBytes) continue;
                    uint16_t block[PIXELS_PER_BLOCK * 3];
                    ConvertBlockScalar(row + static_cast<size_t>(x / PIXELS_PER_BLOCK) * BYTES_PER_BLOCK, block);
                    const uint16_t* p = block + (x % PIXELS_PER_BLOCK) * 3;
                    r = ToByte(p[0] / 65535.0, picture);
                    g = ToByte(p[1] / 65535.0, picture);
                    b = ToByte(p[2] / 65535.0, picture);
                }
                else if (encoding == QStringLiteral("ARGB") || encoding == QStringLiteral("BGRA"))
                {
                    // His ARGB formatter: [A R G B]; BGRA: [B G R A].
                    const uint8_t* p = row + static_cast<size_t>(x) * 4;
                    const bool argb = encoding == QStringLiteral("ARGB");
                    r = ToByte((argb ? p[1] : p[2]) / 255.0, picture);
                    g = ToByte((argb ? p[2] : p[1]) / 255.0, picture);
                    b = ToByte((argb ? p[3] : p[0]) / 255.0, picture);
                }
                else if (encoding == QStringLiteral("v210"))
                {
                    // Six pixels in four little-endian words, limited range:
                    // luma at w0[10..19] w1[0..9] w1[20..29] w2[10..19] w3[0..9]
                    // w3[20..29]; Cb at w0[0..9] w1[10..19] w2[20..29]; Cr at
                    // w0[20..29] w2[0..9] w3[10..19], one pair per two pixels.
                    if ((x / 6 + 1) * 16 > rowBytes) continue;
                    const uint8_t* group = row + static_cast<size_t>(x / 6) * 16;
                    static const int lumaWord[6] = { 0, 1, 1, 2, 3, 3 };
                    static const int lumaShift[6] = { 10, 0, 20, 10, 0, 20 };
                    static const int cbWord[3] = { 0, 1, 2 }, cbShift[3] = { 0, 10, 20 };
                    static const int crWord[3] = { 0, 2, 3 }, crShift[3] = { 20, 0, 10 };
                    const int i = x % 6, pair = i / 2;
                    const auto sample = [&](int word, int bits) { return static_cast<int>((littleEndian(group + word * 4) >> bits) & 0x3FFU); };
                    FromYCbCr((sample(lumaWord[i], lumaShift[i]) - 64) / 876.0,
                        (sample(cbWord[pair], cbShift[pair]) - 512) / 896.0,
                        (sample(crWord[pair], crShift[pair]) - 512) / 896.0, picture, r, g, b);
                }
                else
                {
                    // UYVY and HDYC: [U Y V Y], limited range, one U and V per
                    // two pixels.
                    const size_t even = static_cast<size_t>(x & ~1) * 2;
                    if (static_cast<int>(even) + 3 >= rowBytes) continue;
                    FromYCbCr((row[static_cast<size_t>(x) * 2 + 1] - 16) / 219.0,
                        (row[even] - 128) / 224.0, (row[even + 2] - 128) / 224.0, picture, r, g, b);
                }
                out[ox * 3] = r;
                out[ox * 3 + 1] = g;
                out[ox * 3 + 2] = b;
            }
        }
        return image;
    }
}
