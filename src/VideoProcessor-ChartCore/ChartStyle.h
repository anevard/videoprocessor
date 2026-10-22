#pragma once
#include <QColor>
#include <QFontMetrics>
#include <QPainter>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <algorithm>
#include <array>
#include <cmath>

// What every chart shares: the nits format, the colours, and the look of the
// hover box. Each chart keeps its own layout. A colour marked "his theme"
// copies a value from VpTheme.cpp, which names none for code to read; if his
// theme changes it, change it here.
namespace ChartStyle
{
    inline QString Nits(double nits)
    {
        if (!std::isfinite(nits)) return QStringLiteral("-");
        const int decimals = nits < 1 ? 3 : nits < 10 ? 2 : nits < 100 ? 1 : 0;
        return QString::number(nits, 'f', decimals);
    }

    // His theme: QFrame[card="true"] background, the card the charts sit on.
    inline QColor Card() { return QColor(QStringLiteral("#101b27")); }
    inline QColor Grid() { return QColor(QStringLiteral("#1b2a39")); }
    // His theme: button borders.
    inline QColor Axis() { return QColor(QStringLiteral("#2d475f")); }
    // His theme: QLabel[toneHint="true"] text.
    inline QColor Ink() { return QColor(QStringLiteral("#9fb2c4")); }
    // PQ as mastered, the display's gamut, the keys.
    inline QColor Reference() { return QColor(QStringLiteral("#c9d6e2")); }
    // The line from the pointer to the point the hover box describes.
    inline QColor HoverLine() { return QColor(QStringLiteral("#6f8597")); }
    // libplacebo's default curve, and its chip.
    inline QColor Default() { return QColor(QStringLiteral("#e8a93b")); }
    // The compare chips, in the order they are handed out. The first, for the
    // profile edited when the page opens, is his theme's accent.
    inline const std::array<QColor, 4>& Chips()
    {
        static const std::array<QColor, 4> chips{ QColor(QStringLiteral("#49b6ff")), QColor(QStringLiteral("#6fd08c")),
            QColor(QStringLiteral("#d77be0")), QColor(QStringLiteral("#f0d75a")) };
        return chips;
    }

    // The hover box: the size its lines need, then the box drawn where the
    // chart placed it.
    inline QSizeF HoverBoxSize(const QFontMetrics& metrics, const QStringList& lines)
    {
        int width = 0;
        for (const QString& text : lines) width = std::max(width, metrics.horizontalAdvance(text));
        return QSizeF(width + 16.0, lines.size() * 15.0 + 8.0);
    }
    inline void DrawHoverBox(QPainter& painter, const QRectF& box, const QStringList& lines)
    {
        painter.setPen(QPen(Axis(), 1.0));
        painter.setBrush(QColor(QStringLiteral("#0f1a26")));
        painter.drawRoundedRect(box, 4, 4);
        painter.setPen(QColor(QStringLiteral("#e5eef6")));
        for (int i = 0; i < lines.size(); ++i)
            painter.drawText(QPointF(box.left() + 8, box.top() + 16 + i * 15.0), lines[i]);
    }
}
