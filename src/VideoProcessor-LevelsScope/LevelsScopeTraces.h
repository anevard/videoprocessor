#pragma once
#include <QColor>
#include <QString>
#include <map>
#include <string>
#include <vector>

// The Levels Scope's traces: what each view draws, its name, colour and
// line style, and what each trace is, for its chip's tooltip.
namespace LevelsScopeView
{
    enum class View { Input, InOut };

    struct Trace
    {
        std::string key;
        QString name;
        QColor color;
        bool dots = false, step = false, dashed = false;
        double width = 1.6;
    };

    inline const std::vector<Trace>& InputTraces()
    {
        static const std::vector<Trace> traces = {
            { "detpk", QStringLiteral("Measured peak"), QColor(0x39, 0x87, 0xe5), true },
            // The brightest point detection saw, before libplacebo caps the peak above.
            { "detmax", QStringLiteral("Measured max"), QColor(0xff, 0x3b, 0x3b), true },
            { "l1pk", QStringLiteral("L1 peak"), QColor(0xd9, 0x59, 0x26) },
            { "l1avg", QStringLiteral("L1 average"), QColor(0x90, 0x85, 0xe9), false, false, true },
            { "l1min", QStringLiteral("L1 minimum"), QColor(0xe0, 0x5f, 0xa8) },
            { "detlin", QStringLiteral("Measured avg (linear)"), QColor(0xc9, 0x85, 0x00) },
            { "detpq", QStringLiteral("Measured avg (PQ)"), QColor(0x19, 0x9e, 0x70) },
            { "mblack", QStringLiteral("Mastering black"), QColor(0xf4, 0xf5, 0xf6), false, true, false, 2.4 },
            { "mpeak", QStringLiteral("Mastering peak"), QColor(0x8f, 0x96, 0x9d), false, true, true, 1.6 },
            // HDR10 only: one value for the whole title.
            { "maxcll", QStringLiteral("MaxCLL"), QColor(0xe8, 0xc5, 0x47), false, true, true, 1.6 },
            { "maxfall", QStringLiteral("MaxFALL"), QColor(0x5f, 0xc8, 0xd8), false, true, true, 1.6 },
        };
        return traces;
    }

    inline const std::vector<Trace>& InOutTraces()
    {
        static const std::vector<Trace> traces = {
            { "inband", QStringLiteral("Master: black to peak, avg line"), QColor(255, 255, 255, 71) },
            { "outband", QStringLiteral("On screen: where they landed"), QColor(70, 215, 120, 128) },
            { "p1", QStringLiteral("0.1 nit lands at"), QColor(0xed, 0xa1, 0x00) },
            { "p01", QStringLiteral("0.01 nit lands at"), QColor(0xe6, 0x67, 0x67) },
            { "p001", QStringLiteral("0.001 nit lands at"), QColor(0x4c, 0x9b, 0xf0) },
        };
        return traces;
    }

    // What each chip's trace is, for its tooltip.
    inline QString TraceTip(const std::string& key)
    {
        static const std::map<std::string, QString> tips = {
            { "detpk", QStringLiteral("The brightest pixel's luminance, as libplacebo's peak detection measured it: how bright "
                "it looks, red, green and blue weighted as the eye sees them. Never above Measured max.") },
            { "detmax", QStringLiteral("The brightest single colour channel (red, green or blue), as peak detection measured it, "
                "ignoring a tiny fraction of the very brightest pixels. Equal to Measured peak for white highlights, higher "
                "for saturated ones. L1 peak and MaxCLL are counted the same way, so compare those with this.") },
            { "l1pk", QStringLiteral("Dolby Vision only: the scene's peak as the stream declares it (L1 metadata, set when graded), "
                "by brightest colour channel: compare with Measured max. The ticks on the floor mark each L1 change.") },
            { "l1avg", QStringLiteral("Dolby Vision only: the scene's average as the stream declares it (L1). Averaged in PQ, "
                "so lower than a linear average of the same picture.") },
            { "l1min", QStringLiteral("Dolby Vision only: the scene's black as the stream declares it (L1). 0 is true black.") },
            { "detlin", QStringLiteral("The frame's average brightness from peak detection, averaged in linear light. "
                "The one to compare with MaxFALL.") },
            { "detpq", QStringLiteral("The same frame average taken in PQ code values, which count dark areas for more. "
                "The one to compare with L1 average.") },
            { "mblack", QStringLiteral("The black of the monitor the title was graded on, as the stream declares it. "
                "Almost always 0.0001 nit.") },
            { "mpeak", QStringLiteral("The peak of the monitor the title was graded on, as the stream declares it. "
                "Almost always 1000 nits.") },
            { "maxcll", QStringLiteral("HDR10 only: the brightest pixel anywhere in the title, by brightest colour channel, as the "
                "stream declares it. One value for the whole title: compare with Measured max.") },
            { "maxfall", QStringLiteral("HDR10 only: the brightest frame average anywhere in the title, in linear light, "
                "as the stream declares it. One value for the whole title.") },
            { "inband", QStringLiteral("What came in: from the mastering black up to this moment's peak (measured, else L1). "
                "The white line is its average.") },
            { "outband", QStringLiteral("Where the chosen DTM profile put that range on the screen, through libplacebo's own "
                "tone curve. The green line is the average.") },
            { "p1", QStringLiteral("Where a 0.1-nit level in the source lands on the screen with the chosen DTM profile.") },
            { "p01", QStringLiteral("Where a 0.01-nit level in the source lands on the screen with the chosen DTM profile.") },
            { "p001", QStringLiteral("Where a 0.001-nit level in the source lands on the screen with the chosen DTM profile.") },
        };
        const auto found = tips.find(key);
        return found == tips.end() ? QString() : found->second;
    }

    // The colour a trace is drawn in, opaque, for the readout's labels.
    inline QString TraceColor(const std::string& key)
    {
        for (const auto* list : { &InputTraces(), &InOutTraces() })
            for (const Trace& t : *list)
                if (t.key == key)
                {
                    QColor c = t.color;
                    c.setAlpha(255);
                    return c.name();
                }
        return QString();
    }

    // `text` in a trace's colour, for the readout.
    inline QString Tinted(const std::string& key, const QString& text)
    {
        return QStringLiteral("<span style=\"color:%1\">%2</span>").arg(TraceColor(key), text);
    }
}
