#pragma once
#include "ToneCurveChart.h"
#include "ToneCurveProvider.h"
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QSplitter>
#include <QTabWidget>
#include <QVBoxLayout>
#include <cmath>

// The tone charts column on the Rendering page. Tabs hold the charts (only
// Brightness for now); the scene sliders under the tabs tell every tab what
// scene to imagine and are not VP settings. The profile comes from the page.
class ToneChartPanel : public QWidget
{
public:
    ToneChartPanel()
    {
        // Not "config.*": those names mark configuration edits.
        setObjectName(QStringLiteral("toneCharts"));
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);
        unavailable_ = new QLabel;
        unavailable_->setProperty("help", true);
        unavailable_->setWordWrap(true);
        unavailable_->hide();
        layout->addWidget(unavailable_);

        tabs_ = new QTabWidget;
        auto* brightness = new QWidget;
        auto* charts = new QVBoxLayout(brightness);
        charts->setContentsMargins(4, 8, 4, 4);
        charts->setSpacing(4);
        charts->addWidget(heading(QStringLiteral("Tone curve: source nits in, on-screen nits out")));
        tone_ = new ToneCurveChart(ToneCurveChart::Kind::Tone);
        charts->addWidget(tone_, 1);
        charts->addWidget(heading(QStringLiteral("Gamma tracking: visible difference from PQ (ΔE ITP)")));
        auto* trackingHelp = new QLabel(QStringLiteral(
            "0 is PQ, the source as mastered; below is darker, above is brighter. "
            "The band, ±1, is about one just-noticeable step."));
        trackingHelp->setProperty("toneHint", true);
        trackingHelp->setWordWrap(true);
        charts->addWidget(trackingHelp);
        tracking_ = new ToneCurveChart(ToneCurveChart::Kind::Tracking);
        charts->addWidget(tracking_, 1);
        tabs_->addTab(brightness, QStringLiteral("Brightness"));
        layout->addWidget(tabs_, 1);

        peak_ = sceneSlider(peakLabel_, layout);
        peak_->setObjectName(QStringLiteral("toneCharts.scenePeak"));
        average_ = sceneSlider(averageLabel_, layout);
        average_->setObjectName(QStringLiteral("toneCharts.sceneAverage"));
        peak_->setValue(Position(600.0, kPeakLow, kPeakHigh));
        average_->setValue(Position(15.0, kAverageLow, kAverageHigh));
        sceneNote_ = new QLabel;
        sceneNote_->setProperty("toneHint", true);
        sceneNote_->setWordWrap(true);
        layout->addWidget(sceneNote_);
        connect(peak_, &QSlider::valueChanged, this, [this] { redraw(); });
        connect(average_, &QSlider::valueChanged, this, [this] { redraw(); });
    }

    // The profile as VP would run it; the panel adds the scene.
    void setRequest(const ToneCurveProvider::Request& request)
    {
        request_ = request;
        haveRequest_ = true;
        redraw();
    }

private:
    static constexpr double kPeakLow = 100.0, kPeakHigh = 10000.0;
    static constexpr double kAverageLow = 0.1, kAverageHigh = 1000.0;
    static constexpr int kSamples = 360;

    // The scene sliders move in log steps of nits.
    static int Position(double nits, double low, double high)
    {
        return static_cast<int>(std::lround(1000.0 * std::log(nits / low) / std::log(high / low)));
    }
    static double Value(int position, double low, double high)
    {
        const double nits = low * std::pow(high / low, position / 1000.0);
        const double scale = std::pow(10.0, std::floor(std::log10(nits)) - 1.0);
        return std::round(nits / scale) * scale; // two significant figures
    }

    static QLabel* heading(const QString& text)
    {
        auto* label = new QLabel(text);
        QFont font = label->font();
        font.setBold(true);
        label->setFont(font);
        return label;
    }

    QSlider* sceneSlider(QLabel*& label, QVBoxLayout* layout)
    {
        auto* row = new QHBoxLayout;
        label = new QLabel;
        label->setMinimumWidth(170);
        auto* slider = new QSlider(Qt::Horizontal);
        slider->setRange(0, 1000);
        slider->setSingleStep(5);
        slider->setPageStep(50);
        row->addWidget(label);
        row->addWidget(slider, 1);
        layout->addLayout(row);
        return slider;
    }

    void showUnavailable(const QString& text)
    {
        unavailable_->setText(text);
        unavailable_->show();
        tabs_->hide();
        for (QWidget* widget : { static_cast<QWidget*>(peak_), static_cast<QWidget*>(average_),
            static_cast<QWidget*>(peakLabel_), static_cast<QWidget*>(averageLabel_),
            static_cast<QWidget*>(sceneNote_) })
            widget->hide();
    }

    void redraw()
    {
        if (!haveRequest_) return;
        const auto& functions = ToneCurveProvider::Registered();
        if (!functions.sample || !functions.nitsToPq || !functions.pqToNits)
        {
            showUnavailable(QStringLiteral("Charts need VP Renderer's files."));
            return;
        }
        ToneCurveProvider::Request request = request_;
        request.scenePeakNits = Value(peak_->value(), kPeakLow, kPeakHigh);
        request.sceneAverageNits = std::min(Value(average_->value(), kAverageLow, kAverageHigh),
            request.scenePeakNits);
        request.sourcePq.resize(kSamples + 1);
        for (int i = 0; i <= kSamples; ++i) request.sourcePq[i] = static_cast<double>(i) / kSamples;

        ToneCurveChart::Data chart;
        std::wstring error;
        if (!functions.sample(request, chart.result, error))
        {
            showUnavailable(QStringLiteral("Charts need VP Renderer's files. %1")
                .arg(error.empty() ? QString() : QStringLiteral("Missing: %1").arg(QString::fromStdWString(error))));
            return;
        }
        unavailable_->hide();
        tabs_->show();
        for (QWidget* widget : { static_cast<QWidget*>(peak_), static_cast<QWidget*>(average_),
            static_cast<QWidget*>(peakLabel_), static_cast<QWidget*>(averageLabel_),
            static_cast<QWidget*>(sceneNote_) })
            widget->show();

        chart.sourcePq = request.sourcePq;
        chart.targetPq = functions.nitsToPq(request.targetNits);
        chart.sceneAverage = request.sceneAverageKnown;
        chart.pqToNits = functions.pqToNits;
        for (double nits : { 0.1, 1.0, 10.0, 100.0, 400.0, 1000.0, 4000.0, 10000.0 })
            chart.xTicks.emplace_back(functions.nitsToPq(nits), ToneCurveChart::Nits(nits));
        for (double nits : { 0.1, 1.0, 10.0, 25.0, 50.0, 75.0, 100.0, 150.0, 200.0, 300.0,
            400.0, 600.0, 1000.0, 2000.0, 4000.0 })
            if (nits < request.targetNits * 0.95)
                chart.yTicks.emplace_back(functions.nitsToPq(nits), ToneCurveChart::Nits(nits));
        chart.yTicks.emplace_back(chart.targetPq, ToneCurveChart::Nits(request.targetNits));
        tone_->setData(chart);
        tracking_->setData(chart);

        peakLabel_->setText((request.sceneAverageKnown ? QStringLiteral("Scene peak: %1 nits") :
            QStringLiteral("Mastering peak: %1 nits")).arg(ToneCurveChart::Nits(request.scenePeakNits)));
        averageLabel_->setText(request.sceneAverageKnown ?
            QStringLiteral("Scene average: %1 nits").arg(ToneCurveChart::Nits(request.sceneAverageNits)) :
            QStringLiteral("Scene average: none"));
        average_->setEnabled(request.sceneAverageKnown);
        sceneNote_->setText(request.sceneAverageKnown ?
            QStringLiteral("The scene the charts imagine; not saved, not a VP setting.") :
            QStringLiteral("Peak detection is off, so there is no scene average and the fallback knee sets the knee."));
    }

    QLabel* unavailable_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    ToneCurveChart* tone_ = nullptr;
    ToneCurveChart* tracking_ = nullptr;
    QSlider* peak_ = nullptr;
    QSlider* average_ = nullptr;
    QLabel* peakLabel_ = nullptr;
    QLabel* averageLabel_ = nullptr;
    QLabel* sceneNote_ = nullptr;
    ToneCurveProvider::Request request_;
    bool haveRequest_ = false;
};

// Holds a column's content at the top of the visible part of the page, so it
// stays in view while the page scrolls and never moves when the settings beside
// it change height.
class ToneStickyColumn : public QWidget
{
public:
    explicit ToneStickyColumn(QWidget* content) : content_(content)
    {
        content_->setParent(this);
    }
    QSize sizeHint() const override { return QSize(content_->sizeHint().width(), 0); }
    // Beside the settings the row height comes from them; stacked on a narrow
    // window the column needs room of its own.
    QSize minimumSizeHint() const override
    {
        const auto* splitter = qobject_cast<const QSplitter*>(parentWidget());
        const bool stacked = splitter && splitter->orientation() == Qt::Vertical;
        return QSize(content_->minimumSizeHint().width(), stacked ? 420 : 0);
    }

protected:
    void showEvent(QShowEvent* event) override
    {
        QWidget::showEvent(event);
        if (!area_)
            for (QWidget* parent = parentWidget(); parent; parent = parent->parentWidget())
                if (auto* scroll = qobject_cast<QScrollArea*>(parent))
                {
                    area_ = scroll;
                    connect(scroll->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { place(); });
                    scroll->viewport()->installEventFilter(this);
                    break;
                }
        place();
    }
    void resizeEvent(QResizeEvent* event) override { QWidget::resizeEvent(event); place(); }
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (area_ && watched == area_->viewport() && event->type() == QEvent::Resize) place();
        return QWidget::eventFilter(watched, event);
    }

private:
    void place()
    {
        const int minimum = std::max(content_->minimumSizeHint().height(), 420);
        int top = 0, visible = height();
        if (area_)
        {
            const int offset = -mapTo(area_->viewport(), QPoint(0, 0)).y();
            top = std::max(0, offset);
            visible = area_->viewport()->height() - 8;
        }
        const int contentHeight = std::min(height(), std::max(minimum, visible));
        top = std::clamp(top, 0, std::max(0, height() - contentHeight));
        content_->setGeometry(0, top, width(), contentHeight);
    }

    QWidget* content_;
    QPointer<QScrollArea> area_;
};
