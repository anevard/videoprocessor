#pragma once
#include "ToneCurveChart.h"
#include "ToneCurveProvider.h"
#include "ToneProfileFile.h"
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QToolButton>
#include <QStackedWidget>
#include <QTabBar>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <utility>
#include <vector>

// The tone charts column on the Rendering page. Tabs hold the charts (only
// Brightness for now); the scene buttons under the tab bar, above the charts,
// tell every tab what scene to imagine and are not VP settings. The profile comes from the page.
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

        // The tab bar, then what every tab shares (the scenes), then the
        // tab pages. His theme styles no tab page, so Windows would paint the
        // pages (white in light mode); they get his card colours on every PC.
        tabs_ = new QWidget;
        auto* area = new QVBoxLayout(tabs_);
        area->setContentsMargins(0, 0, 0, 0);
        area->setSpacing(6);
        tabBar_ = new QTabBar;
        tabBar_->setObjectName(QStringLiteral("toneChartsTabs"));
        tabBar_->setDrawBase(false);
        tabBar_->setExpanding(false); // a tab, not a full-width bar
        area->addWidget(tabBar_);
        shared_ = new QVBoxLayout;
        shared_->setSpacing(4);
        area->addLayout(shared_);
        pages_ = new QStackedWidget;
        pages_->setObjectName(QStringLiteral("toneChartsPages"));
        pages_->setStyleSheet(QStringLiteral(
            "#toneChartsPages { background: #101b27; border: 1px solid #273d53; border-radius: 4px; }"
            " #toneChartsBrightness { background: #101b27; }"
            " #toneChartsDivider { background: #273d53; }"));
        area->addWidget(pages_, 1);
        connect(tabBar_, &QTabBar::currentChanged, pages_, &QStackedWidget::setCurrentIndex);
        auto* brightness = new QWidget;
        brightness->setObjectName(QStringLiteral("toneChartsBrightness"));
        brightness->setAttribute(Qt::WA_StyledBackground, true);
        auto* charts = new QVBoxLayout(brightness);
        charts->setContentsMargins(4, 8, 4, 4);
        charts->setSpacing(4);
        charts->addWidget(heading(QStringLiteral("Tone curve: source nits in, on-screen nits out")));
        tone_ = new ToneCurveChart(ToneCurveChart::Kind::Tone);
        charts->addWidget(tone_, 1);
        // A thin line in the box's border colour marks where one chart ends.
        auto* divider = new QWidget;
        divider->setObjectName(QStringLiteral("toneChartsDivider"));
        divider->setAttribute(Qt::WA_StyledBackground, true);
        divider->setFixedHeight(1);
        charts->addSpacing(2);
        charts->addWidget(divider);
        charts->addSpacing(2);
        charts->addWidget(heading(QStringLiteral("Gamma tracking: visible difference from PQ (ΔE ITP)")));
        auto* trackingHelp = new QLabel(QStringLiteral(
            "0 is PQ, the source as mastered; below is darker, above is brighter. "
            "The band, ±1, is about one just-noticeable step."));
        trackingHelp->setProperty("toneHint", true);
        trackingHelp->setWordWrap(true);
        charts->addWidget(trackingHelp);
        tracking_ = new ToneCurveChart(ToneCurveChart::Kind::Tracking);
        charts->addWidget(tracking_, 1);
        charts->addStretch(0); // what the charts' shape leaves over, below them
        pages_->addWidget(brightness);
        tabBar_->addTab(QStringLiteral("Brightness"));
        layout->addWidget(tabs_, 1);

        // Compare with: libplacebo's default curve, then up to four profiles or
        // exported profile files, the edited profile among them in bold. The
        // chips are the legend.
        auto* compareRow = new QHBoxLayout;
        compareRow->setSpacing(6);
        compareRow->addWidget(new QLabel(QStringLiteral("Compare with:")));
        defaultChip_ = chip(QStringLiteral("libplacebo default"), QColor(QStringLiteral("#e8a93b")), true);
        defaultChip_->setObjectName(QStringLiteral("toneCharts.compareDefault"));
        defaultChip_->setChecked(true);
        defaultChip_->setToolTip(QStringLiteral("Show or hide libplacebo's default curve."));
        connect(defaultChip_, &QToolButton::toggled, this, [this] { redraw(); });
        compareRow->addWidget(defaultChip_);
        chips_ = new QHBoxLayout;
        chips_->setSpacing(6);
        compareRow->addLayout(chips_);
        // A push button, so his theme draws it (a tool button is Windows-styled).
        addCompare_ = new QPushButton(QStringLiteral("+ Add"));
        addCompare_->setObjectName(QStringLiteral("toneCharts.compareAdd"));
        auto* menu = new QMenu(addCompare_);
        addCompare_->setMenu(menu);
        connect(menu, &QMenu::aboutToShow, this, [this, menu] { fillCompareMenu(menu); });
        compareRow->addWidget(addCompare_);
        compareRow->addStretch(1);
        shared_->addLayout(compareRow);

        // The scenes: one row for peak detection on, one for off; only the row
        // the edited profile's peak detection allows is shown.
        for (int set = 0; set < 2; ++set)
        {
            sceneRows_[set] = new QWidget;
            auto* row = new QHBoxLayout(sceneRows_[set]);
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(6);
            row->addWidget(new QLabel(QStringLiteral("Scene:")));
            const auto& scenes = Scenes(set == 0);
            for (size_t i = 0; i < scenes.size(); ++i)
            {
                auto* button = new QPushButton(QString::fromLatin1(scenes[i].name));
                button->setObjectName(QStringLiteral("toneCharts.scene.%1").arg(QString::fromLatin1(scenes[i].name)));
                button->setCheckable(true);
                button->setChecked(i == sceneIndex_[set]);
                connect(button, &QPushButton::clicked, this, [this, set, i] { sceneIndex_[set] = i; redraw(); });
                sceneButtons_[set].push_back(button);
                row->addWidget(button);
            }
            row->addStretch(1);
            shared_->addWidget(sceneRows_[set]);
        }
        sceneNote_ = new QLabel;
        sceneNote_->setProperty("toneHint", true);
        sceneNote_->setWordWrap(true);
        shared_->addWidget(sceneNote_);
    }

    // The profile as VP would run it; the panel adds the scene.
    void setRequest(const ToneCurveProvider::Request& request)
    {
        request_ = request;
        haveRequest_ = true;
        redraw();
    }

    // Where compared profiles come from. The page lists its profiles
    // (section, name), names the edited one, and turns a profile or an
    // exported file's values into a request exactly as for the edited one.
    struct CompareSources
    {
        std::function<std::vector<std::pair<QString, QString>>()> profiles;
        std::function<QString()> current;
        std::function<ToneCurveProvider::Request(const QString& section)> profileRequest;
        std::function<ToneCurveProvider::Request(const std::map<QString, QString>& values)> valuesRequest;
    };
    void setCompareSources(CompareSources sources) { sources_ = std::move(sources); }

private:
    static constexpr size_t kMaxCompared = 4;
    struct Compared
    {
        int id = 0;
        QString name;
        QColor color;
        QString section;                     // a profile on this page, or
        std::map<QString, QString> values;   // an exported file's settings
        bool fromFile = false;
        bool bySelecting = false;            // added by selecting it, not by "+ Add"
    };

    // A switch chip greys out when off; a compare chip removes itself; the
    // edited profile's chip is bold and stays.
    static QToolButton* chip(const QString& text, const QColor& color, bool toggle = false, bool bold = false)
    {
        auto* button = new QToolButton;
        button->setText(QStringLiteral("● ") + text);
        button->setCheckable(toggle);
        button->setStyleSheet(QStringLiteral(
            "QToolButton { color: %1; background: #101d2a; border: %2px solid %1; border-radius: 10px; padding: 1px 8px;%3 }")
            .arg(color.name(), bold ? QStringLiteral("2") : QStringLiteral("1"),
                bold ? QStringLiteral(" font-weight: bold;") : QString()) + (toggle ? QStringLiteral(
            " QToolButton:!checked { color: #63798c; border-color: #2c4659; }") : QString()));
        return button;
    }

    QColor nextColor() const
    {
        // Blue first, so the profile edited when the page opens is blue.
        for (const char* candidate : { "#49b6ff", "#6fd08c", "#d77be0", "#f0d75a" })
        {
            const QColor color(QString::fromLatin1(candidate));
            if (std::none_of(compared_.begin(), compared_.end(),
                [&](const Compared& compared) { return compared.color == color; }))
                return color;
        }
        return QColor(QStringLiteral("#49b6ff"));
    }

    void fillCompareMenu(QMenu* menu)
    {
        menu->clear();
        const bool full = compared_.size() >= kMaxCompared;
        const QString current = sources_.current ? sources_.current() : QString();
        int others = 0;
        if (sources_.profiles)
            for (const auto& profile : sources_.profiles())
            {
                const QString section = profile.first, name = profile.second;
                if (section.compare(current, Qt::CaseInsensitive) == 0)
                {
                    // Listed, so a new profile is visibly there: its chip is the bold one.
                    menu->addAction(name + QStringLiteral(" (editing)"))->setEnabled(false);
                    continue;
                }
                const bool already = std::any_of(compared_.begin(), compared_.end(), [&](const Compared& compared) {
                    return !compared.fromFile && compared.section.compare(section, Qt::CaseInsensitive) == 0;
                });
                ++others;
                QAction* action = menu->addAction(name);
                action->setEnabled(!full && !already);
                connect(action, &QAction::triggered, this, [this, section, name] {
                    addCompared(Compared{ 0, name, nextColor(), section, {}, false });
                });
            }
        if (others == 0) menu->addAction(QStringLiteral("No other profiles"))->setEnabled(false);
        menu->addSeparator();
        QAction* file = menu->addAction(QStringLiteral("From file..."));
        file->setEnabled(!full);
        connect(file, &QAction::triggered, this, [this] { compareFile(); });
    }

    // An exported profile, drawn without importing it.
    void compareFile()
    {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Compare with an exported profile"),
            QString(), QStringLiteral("VP Rendering / DTM profile (*.vpprofile);;All files (*)"));
        if (path.isEmpty()) return;
        ToneProfileFile::Profile profile;
        QString error;
        if (!ToneProfileFile::Read(path, profile, error))
        {
            QMessageBox::warning(this, QStringLiteral("Compare with a file"), error);
            return;
        }
        std::map<QString, QString> values(profile.settings.begin(), profile.settings.end());
        const auto label = values.find(QStringLiteral("label"));
        const QString name = (label != values.end() && !label->second.isEmpty() ? label->second :
            QFileInfo(path).completeBaseName()) + QStringLiteral(" (file)");
        addCompared(Compared{ 0, name, nextColor(), QString(), std::move(values), true });
    }

    void addCompared(Compared compared)
    {
        if (compared_.size() >= kMaxCompared) return;
        compared.id = ++lastCompareId_;
        compared_.push_back(std::move(compared));
        rebuildChips();
        redraw();
    }

    void rebuildChips()
    {
        while (QLayoutItem* item = chips_->takeAt(0))
        {
            // Later: a chip's own click rebuilds the chips.
            if (QWidget* widget = item->widget()) { widget->hide(); widget->deleteLater(); }
            delete item;
        }
        for (const Compared& compared : compared_)
        {
            if (IsEdited(compared))
            {
                auto* button = chip(compared.name, compared.color, false, true);
                button->setObjectName(QStringLiteral("toneCharts.compare%1").arg(compared.id));
                button->setToolTip(QStringLiteral("Being edited: select another profile to move on."));
                chips_->addWidget(button);
                continue;
            }
            auto* button = chip(compared.name + QStringLiteral("  ✕"), compared.color);
            button->setObjectName(QStringLiteral("toneCharts.compare%1").arg(compared.id));
            button->setToolTip(QStringLiteral("Stop comparing %1.").arg(compared.name));
            const int id = compared.id;
            connect(button, &QToolButton::clicked, this, [this, id] {
                compared_.erase(std::remove_if(compared_.begin(), compared_.end(),
                    [id](const Compared& entry) { return entry.id == id; }), compared_.end());
                rebuildChips();
                redraw();
            });
            chips_->addWidget(button);
        }
    }

    bool IsEdited(const Compared& compared) const
    {
        return !compared.fromFile && !edited_.isEmpty() && compared.section.compare(edited_, Qt::CaseInsensitive) == 0;
    }

    // The edited profile always has a chip. Selecting one not in the row adds
    // it; when the row is full, the oldest chip added by selecting makes way
    // and the new one takes its colour. "+ Add" chips make way only when no
    // chip was added by selecting. Names follow the page.
    void followEdited(const std::vector<std::pair<QString, QString>>& profiles, const QString& current)
    {
        bool changed = edited_.compare(current, Qt::CaseInsensitive) != 0;
        edited_ = current;
        QString currentName;
        for (const auto& profile : profiles)
        {
            if (profile.first.compare(current, Qt::CaseInsensitive) == 0) currentName = profile.second;
            for (Compared& compared : compared_)
                if (!compared.fromFile && compared.section.compare(profile.first, Qt::CaseInsensitive) == 0
                    && compared.name != profile.second)
                {
                    compared.name = profile.second;
                    changed = true;
                }
        }
        if (!currentName.isEmpty() && std::none_of(compared_.begin(), compared_.end(),
            [this](const Compared& compared) { return IsEdited(compared); }))
        {
            if (compared_.size() >= kMaxCompared)
            {
                const auto selected = std::find_if(compared_.begin(), compared_.end(),
                    [](const Compared& compared) { return compared.bySelecting; });
                compared_.erase(selected != compared_.end() ? selected : compared_.begin());
            }
            compared_.push_back(Compared{ ++lastCompareId_, currentName, nextColor(), current, {}, false, true });
            changed = true;
        }
        if (changed) rebuildChips();
    }
    static constexpr int kSamples = 360;

    // A scene the charts imagine, in nits. Measured scenes are what peak
    // detection reports (peak, average) for a source mastered at `mastering`;
    // static scenes have only the InfoFrame mastering peak (0: none, read as
    // 10000).
    struct Scene { const char* name; double peak; double average; double mastering; };
    static const std::vector<Scene>& Scenes(bool measured)
    {
        static const std::vector<Scene> withDetection = {
            { "Dark", 200, 2, 1000 }, { "Mid", 600, 15, 1000 }, { "Bright", 1000, 60, 1000 },
            { "Very bright", 1000, 150, 1000 }, { "Hot master", 1300, 60, 1000 } };
        static const std::vector<Scene> withoutDetection = {
            { "1000 nits", 1000, 0, 1000 }, { "4000 nits", 4000, 0, 4000 }, { "No metadata", 10000, 0, 0 } };
        return measured ? withDetection : withoutDetection;
    }

    static QLabel* heading(const QString& text)
    {
        auto* label = new QLabel(text);
        QFont font = label->font();
        font.setBold(true);
        label->setFont(font);
        return label;
    }

    void showUnavailable(const QString& text)
    {
        unavailable_->setText(text);
        unavailable_->show();
        tabs_->hide();
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
        if (!request_.betaRenderer)
        {
            showUnavailable(QStringLiteral("The charts show VP Renderer (Beta) only. Select it under Renderer to see them."));
            return;
        }
        ToneCurveProvider::Request request = request_;
        const int set = request.sceneAverageKnown ? 0 : 1;
        sceneRows_[set]->show();
        sceneRows_[1 - set]->hide();
        for (size_t i = 0; i < sceneButtons_[set].size(); ++i)
            sceneButtons_[set][i]->setChecked(i == sceneIndex_[set]);
        const Scene& scene = Scenes(set == 0)[sceneIndex_[set]];
        request.scenePeakNits = scene.peak;
        request.sceneAverageNits = scene.average;
        request.masteringPeakNits = scene.mastering;
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
        // Compared profiles, as the page resolves them now. One whose profile
        // is gone is dropped; the edited profile is drawn bold, in its chip's
        // colour, from the page's live values.
        chart.showDefault = defaultChip_->isChecked();
        if (sources_.profiles && sources_.profileRequest && sources_.valuesRequest)
        {
            const auto profiles = sources_.profiles();
            const QString current = sources_.current ? sources_.current() : QString();
            const size_t before = compared_.size();
            compared_.erase(std::remove_if(compared_.begin(), compared_.end(), [&](const Compared& compared) {
                return !compared.fromFile && std::none_of(profiles.begin(), profiles.end(),
                    [&](const auto& profile) { return profile.first.compare(compared.section, Qt::CaseInsensitive) == 0; });
            }), compared_.end());
            if (compared_.size() != before) rebuildChips();
            followEdited(profiles, current);
            for (const Compared& compared : compared_)
            {
                if (IsEdited(compared))
                {
                    chart.profileName = compared.name;
                    chart.profileColor = compared.color;
                    continue;
                }
                ToneCurveProvider::Request other = compared.fromFile ?
                    sources_.valuesRequest(compared.values) : sources_.profileRequest(compared.section);
                other.scenePeakNits = request.scenePeakNits;
                other.sceneAverageNits = request.sceneAverageNits;
                other.masteringPeakNits = request.masteringPeakNits;
                other.sourcePq = request.sourcePq;
                ToneCurveProvider::Result result;
                std::wstring ignored;
                if (functions.sample(other, result, ignored))
                    chart.compares.push_back({ compared.name, compared.color, result.profilePq,
                        functions.nitsToPq(other.targetNits) });
            }
        }
        tone_->setData(chart);
        tracking_->setData(chart);

        // What the scene is, then what libplacebo does to it beyond the curve.
        QStringList note;
        note << (request.sceneAverageKnown ?
            QStringLiteral("Measured peak %1, average %2, mastered at %3 nits.")
                .arg(ToneCurveChart::Nits(scene.peak), ToneCurveChart::Nits(scene.average), ToneCurveChart::Nits(scene.mastering)) :
            scene.mastering > 0 ?
            QStringLiteral("Peak detection is off: only the mastering peak, %1 nits; no scene average, so the fallback knee sets the knee.")
                .arg(ToneCurveChart::Nits(scene.mastering)) :
            QStringLiteral("Peak detection is off and there is no metadata: libplacebo assumes 10000 nits; no scene average, so the fallback knee sets the knee."));
        const auto& result = chart.result;
        if (result.virtualTargetNits > 0)
            note << QStringLiteral("Dynamic target: tone mapped to a virtual %1 nits, then the whole picture ×%2.")
                .arg(ToneCurveChart::Nits(result.virtualTargetNits)).arg(result.exposure, 0, 'f', 2);
        if (result.masteringClip > 1e-3)
            note << QStringLiteral("Mastering soft clip at strength %1, from %2 nits.")
                .arg(result.masteringClip, 0, 'f', 2).arg(ToneCurveChart::Nits(0.95 * scene.mastering));
        note << QStringLiteral("Film strength and gamut overflow correction change colour, not this grey curve. "
            "The scene is not saved and is not a VP setting; VP smooths the measurement over time.");
        sceneNote_->setText(note.join(QLatin1Char(' ')));
    }

    QLabel* unavailable_ = nullptr;
    CompareSources sources_;
    std::vector<Compared> compared_;
    QString edited_; // the page's selected profile section
    int lastCompareId_ = 0;
    QToolButton* defaultChip_ = nullptr;
    QHBoxLayout* chips_ = nullptr;
    QPushButton* addCompare_ = nullptr;
    QWidget* tabs_ = nullptr; // the tab bar, the shared scene controls and the pages
    QTabBar* tabBar_ = nullptr;
    QVBoxLayout* shared_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    ToneCurveChart* tone_ = nullptr;
    ToneCurveChart* tracking_ = nullptr;
    QWidget* sceneRows_[2] = {}; // measured, static
    std::vector<QPushButton*> sceneButtons_[2];
    size_t sceneIndex_[2] = { 1, 0 }; // Mid; 1000 nits
    QLabel* sceneNote_ = nullptr;
    ToneCurveProvider::Request request_;
    bool haveRequest_ = false;
};
