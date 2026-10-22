#pragma once
#include "LevelsScopeData.h"
#include "LevelsScopeView.h"
#include <ConfigEditorCore.h>
#include <RenderingBuiltInDefault.h>
#include <ToneProfileRequest.h>
#include <QButtonGroup>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>
#include <map>
#include <set>

// The Levels Scope page: open logs, pick a profile of a saved
// VideoProcessor.cfg, pick sections, and read the chart. Everything the page
// shows about where levels land comes from the Config page's tone engine.
// Built to become a Config page later; for now the helper's window hosts it.
namespace LevelsScopePanel
{
    class Panel : public QWidget
    {
    public:
        // `settingsName` keeps a snapshot run's choices apart from the window's.
        explicit Panel(QWidget* parent = nullptr, const QString& settingsName = QStringLiteral("Levels Scope"),
            bool restore = true)
            : QWidget(parent), settings_(QStringLiteral("VideoProcessor"), settingsName)
        {
            // His theme paints only #root, QMainWindow and his own widgets dark;
            // the rest comes from the colours his Config page and charts use.
            setObjectName(QStringLiteral("root"));
            setAttribute(Qt::WA_StyledBackground, true);
            setStyleSheet(QStringLiteral(
                "QTreeWidget { color: #e8f1f8; background: #0b1520; border: 1px solid #3a5269; border-radius: 4px; outline: 0; }"
                " QTreeWidget::item { padding: 3px 6px; }"
                " QTreeWidget::item:hover { background: #152536; }"
                " QTreeWidget::item:selected { background: #173b58; color: #f0f8ff; }"
                " QPushButton:checked { color: #f8fcff; background: #1479c4; border-color: #248dd6; }"));
            auto* layout = new QVBoxLayout(this);
            layout->setContentsMargins(12, 12, 12, 12);
            layout->setSpacing(8);

            // Logs and the profile.
            auto* sources = new QHBoxLayout;
            auto* openLogs = new QPushButton(QStringLiteral("Open logs…"));
            auto* openFolder = new QPushButton(QStringLiteral("Open logs folder…"));
            auto* openConfig = new QPushButton(QStringLiteral("Config…"));
            profiles_ = new QComboBox;
            profiles_->setMinimumWidth(180);
            // Ignored width: a long path is cut short (whole path on hover)
            // rather than widening the window past the screen.
            configLabel_ = new QLabel;
            configLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            configLabel_->setProperty("help", true);
            sources->addWidget(openLogs);
            sources->addWidget(openFolder);
            sources->addSpacing(16);
            sources->addWidget(openConfig);
            sources->addWidget(new QLabel(QStringLiteral("DTM profile")));
            sources->addWidget(profiles_);
            sources->addWidget(configLabel_, 1);
            layout->addLayout(sources);
            status_ = new QLabel;
            status_->setWordWrap(true);
            status_->setProperty("help", true);
            layout->addWidget(status_);
            // The open logs, each removable.
            logs_ = new QHBoxLayout;
            logs_->setSpacing(4);
            auto* logsRow = new QHBoxLayout;
            logsRow->addLayout(logs_);
            logsRow->addStretch(1);
            layout->addLayout(logsRow);

            // Sections: tick to show, double-click a name to rename.
            sections_ = new QTreeWidget;
            sections_->setColumnCount(5);
            sections_->setHeaderLabels({ QStringLiteral("Section"), QStringLiteral("Start"), QStringLiteral("Length"),
                QStringLiteral("Mastering (metadata)"), QStringLiteral("Build") });
            sections_->setRootIsDecorated(false);
            sections_->setEditTriggers(QAbstractItemView::NoEditTriggers);
            sections_->setMaximumHeight(150);
            sections_->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
            layout->addWidget(sections_);

            // The view, how to zoom, and the traces of the view.
            auto* controls = new QHBoxLayout;
            controls->setSpacing(4);
            const auto group = [&](const QStringList& names, int checked, auto onPick) {
                auto* buttons = new QButtonGroup(this);
                if (!viewButtons_) viewButtons_ = buttons;
                for (int i = 0; i < names.size(); ++i)
                {
                    auto* b = new QPushButton(names[i]);
                    b->setCheckable(true);
                    b->setChecked(i == checked);
                    buttons->addButton(b, i);
                    controls->addWidget(b);
                }
                connect(buttons, &QButtonGroup::idClicked, this, onPick);
                controls->addSpacing(14);
            };
            chart_ = new LevelsScopeView::Chart;
            const int view = settings_.value(QStringLiteral("view"), 1).toInt();
            group({ QStringLiteral("Input scope"), QStringLiteral("In vs out") }, view, [this](int id) {
                chart_->setView(id == 0 ? LevelsScopeView::View::Input : LevelsScopeView::View::InOut);
                settings_.setValue(QStringLiteral("view"), id);
                rebuildLegend();
                syncScroll();
            });
            // No zoom buttons: the chart zooms and scrolls both ways itself.
            auto* howTo = new QLabel(QStringLiteral(
                "Wheel: zoom time  ·  Ctrl+wheel: zoom brightness  ·  drag: move  ·  double-click: show all"));
            howTo->setProperty("help", true);
            controls->addWidget(howTo);
            controls->addStretch(1);
            layout->addLayout(controls);
            // The view's traces on a row of their own, so eleven chips never
            // set the window's minimum width.
            legend_ = new QHBoxLayout;
            legend_->setSpacing(4);
            auto* legendRow = new QHBoxLayout;
            legendRow->addLayout(legend_);
            legendRow->addStretch(1);
            layout->addLayout(legendRow);

            chart_->setView(view == 0 ? LevelsScopeView::View::Input : LevelsScopeView::View::InOut);
            for (const auto* list : { &LevelsScopeView::InputTraces(), &LevelsScopeView::InOutTraces() })
                for (const auto& t : *list)
                    chart_->setShown(t.key, settings_.value(QStringLiteral("shown/") + QString::fromStdString(t.key), true).toBool());
            chart_->setNames([this](const LevelsScopeData::Section& s) { return shortName(s); });
            // The chart with its brightness scrollbar on the right, time below.
            auto* chartRow = new QHBoxLayout;
            chartRow->setSpacing(2);
            chartRow->addWidget(chart_, 1);
            brightness_ = new QScrollBar(Qt::Vertical);
            brightness_->setRange(0, 0);
            chartRow->addWidget(brightness_);
            layout->addLayout(chartRow, 1);
            scroll_ = new QScrollBar(Qt::Horizontal);
            scroll_->setRange(0, 1000);
            layout->addWidget(scroll_);
            readout_ = new QLabel;
            readout_->setTextFormat(Qt::RichText);
            // Fixed size: a long readout is cut off rather than resizing the window.
            readout_->setFixedHeight(24);
            readout_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            layout->addWidget(readout_);
            note_ = new QLabel;
            note_->setWordWrap(true);
            note_->setProperty("help", true);
            layout->addWidget(note_);

            connect(openLogs, &QPushButton::clicked, this, [this] {
                const QStringList files = QFileDialog::getOpenFileNames(this, QStringLiteral("Open vp.log files"),
                    settings_.value(QStringLiteral("lastDir")).toString(), QStringLiteral("VP logs (*.log *.log.* vp*);;All files (*)"));
                if (files.isEmpty()) return;
                settings_.setValue(QStringLiteral("lastDir"), QFileInfo(files.first()).absolutePath());
                loadLogs(files);
            });
            connect(openFolder, &QPushButton::clicked, this, [this] {
                const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Open a logs folder"),
                    settings_.value(QStringLiteral("lastDir")).toString());
                if (dir.isEmpty()) return;
                settings_.setValue(QStringLiteral("lastDir"), dir);
                QStringList files;
                QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
                while (it.hasNext()) files << it.next();
                loadLogs(files);
            });
            connect(openConfig, &QPushButton::clicked, this, [this] {
                const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Open VideoProcessor.cfg"),
                    QFileInfo(configPath_).absolutePath(), QStringLiteral("VP configuration (*.cfg);;All files (*)"));
                if (!file.isEmpty()) loadConfig(file);
            });
            connect(profiles_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
                settings_.setValue(QStringLiteral("profile"), profiles_->currentData().toString());
                place();
            });
            connect(sections_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
                if (filling_) return;
                const QString id = item->data(0, Qt::UserRole).toString();
                if (column == 0)
                {
                    picked_[id] = item->checkState(0) == Qt::Checked;
                    const QString text = item->text(0).trimmed();
                    const QString kind = item->data(0, Qt::UserRole + 1).toString();
                    if (text.isEmpty() || text == kind) names_.erase(id); else names_[id] = text;
                    saveSections();
                }
                showPicked();
            });
            connect(sections_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
                sections_->editItem(item, 0);
            });
            connect(scroll_, &QScrollBar::valueChanged, this, [this](int v) { if (!syncing_) chart_->setPosition(v); });
            connect(brightness_, &QScrollBar::valueChanged, this, [this](int v) { if (!syncing_) chart_->setBrightnessPosition(v); });
            chart_->onMoved = [this] { syncScroll(); };
            chart_->onHover = [this](const QString& text) { readout_->setText(text); };

            loadSections();
            if (!restore)
            {
                rebuildLegend();
                updateNote();
                return;
            }
            QString config = settings_.value(QStringLiteral("config")).toString();
            if (config.isEmpty() || !QFileInfo::exists(config))
            {
                // Beside the Config application's folder: <portable>\VideoProcessor.cfg.
                const QString guess = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../VideoProcessor.cfg"));
                if (QFileInfo::exists(guess)) config = QFileInfo(guess).canonicalFilePath();
            }
            if (!config.isEmpty()) loadConfig(config);
            rebuildLegend();
            // Starts blank: logs are opened each time, never reopened from last time.
            settings_.remove(QStringLiteral("logs"));
            reload();
        }

        // For a snapshot run: open these, pick that profile and view.
        void open(const QString& config, const QStringList& logs)
        {
            loadConfig(config);
            loadLogs(logs);
        }
        bool selectProfile(const QString& section)
        {
            const int i = profiles_->findData(section, Qt::UserRole, Qt::MatchFixedString);
            if (i < 0) return false;
            profiles_->setCurrentIndex(i);
            place();
            return true;
        }
        void showView(int id) { viewButtons_->button(id)->click(); }
        LevelsScopeView::Chart* chart() const { return chart_; }
        QString status() const { return status_->text(); }

    private:
        // Rendering profiles that set any tone or target key: the DTM profiles.
        static bool IsDtmProfile(const ConfigEditorCore::ConfigDocument& doc, const std::string& section)
        {
            for (const char* key : { "tone_mapping", "peak_detection", "sdr_target_nits", "dynamic_target", "knee_default" })
                if (!doc.Get(section.c_str(), key).empty()) return true;
            return false;
        }

        void loadConfig(const QString& path)
        {
            ConfigEditorCore::ConfigDocument doc;
            std::wstring error;
            if (!doc.Load(path.toStdWString(), error))
            {
                status_->setText(QStringLiteral("Could not read %1: %2").arg(path, QString::fromStdWString(error)));
                return;
            }
            document_ = doc;
            configPath_ = path;
            settings_.setValue(QStringLiteral("config"), path);
            configLabel_->setText(QDir::toNativeSeparators(path));
            configLabel_->setToolTip(QDir::toNativeSeparators(path));
            const QString wanted = settings_.value(QStringLiteral("profile")).toString();
            const QSignalBlocker block(profiles_);
            profiles_->clear();
            for (const std::string& section : document_.SectionNames())
            {
                const QString name = QString::fromStdString(section);
                if (!name.startsWith(QStringLiteral("vprenderer"), Qt::CaseInsensitive) || !IsDtmProfile(document_, section)) continue;
                const QString label = QString::fromLocal8Bit(document_.Get(section.c_str(), "label").c_str()).trimmed();
                const QString shown = !label.isEmpty() ? label :
                    name.contains(u'.') ? name.mid(name.indexOf(u'.') + 1) : name;
                profiles_->addItem(shown, name);
                if (name.compare(wanted, Qt::CaseInsensitive) == 0) profiles_->setCurrentIndex(profiles_->count() - 1);
            }
            place();
        }

        // Opened logs join those already open (a sample in two files counts once).
        void loadLogs(const QStringList& paths)
        {
            for (const QString& path : paths)
            {
                const QString file = QFileInfo(path).absoluteFilePath();
                if (!files_.contains(file, Qt::CaseInsensitive)) files_ << file;
            }
            reload();
        }

        void removeLog(const QString& file)
        {
            files_.removeAll(file);
            reload();
        }

        // Every open log read again into sections, then placed and drawn.
        void reload()
        {
            library_ = LevelsScopeData::Read(files_);
            QString text = files_.isEmpty() ? QStringLiteral("No logs open.") : library_.notes.join(QStringLiteral(" · "));
            if (!files_.isEmpty() && library_.sections.empty())
                text += QStringLiteral(" No sections of 2 minutes or more: the logs need a build that writes "
                                       "\"Dolby Vision luminance:\" or \"HDR luminance:\" lines.");
            status_->setText(text);
            rebuildLogChips();
            place();
        }

        // One chip per open log file: its folder and name, × removes it.
        void rebuildLogChips()
        {
            while (QLayoutItem* item = logs_->takeAt(0))
            {
                delete item->widget();
                delete item;
            }
            for (const QString& file : files_)
            {
                const QFileInfo info(file);
                auto* chip = new QToolButton;
                chip->setText(info.dir().dirName() + u'/' + info.fileName() + QStringLiteral("  ×"));
                chip->setToolTip(QStringLiteral("Remove %1").arg(QDir::toNativeSeparators(file)));
                chip->setStyleSheet(QStringLiteral(
                    "QToolButton { color: #dce8f2; background: #101d2a; border: 1px solid #35607f; border-radius: 10px; padding: 1px 8px; }"
                    " QToolButton:hover { border-color: #e0645b; color: #ffb4ad; }"));
                connect(chip, &QToolButton::clicked, this, [this, file] { removeLog(file); });
                logs_->addWidget(chip);
            }
        }

        // Where the chosen profile puts every sample, then redraw.
        void place()
        {
            placed_ = false;
            placeError_.clear();
            if (profiles_->count() > 0 && !library_.sections.empty())
            {
                const std::string section = profiles_->currentData().toString().toStdString();
                request_ = ToneProfileRequest::From([&](const char* key) {
                    return QString::fromLocal8Bit(document_.Get(section.c_str(), key).c_str()).trimmed();
                }, RenderingBuiltInDefault);
                std::wstring error;
                placed_ = LevelsScopeData::Place(library_, [this](const QString&) -> const ToneCurveProvider::Request& { return request_; }, error);
                if (!placed_) placeError_ = QString::fromStdWString(error);
                chart_->setScreen(request_.targetNits, request_.blackNits);
                chart_->refresh();
            }
            fillSections();
            updateNote();
        }

        QString shortName(const LevelsScopeData::Section& s) const
        {
            const auto found = names_.find(s.id);
            if (found != names_.end()) return found->second;
            if (s.kind == QStringLiteral("Apple TV home screen")) return QStringLiteral("Home screen");
            if (s.kind == QStringLiteral("Short DV clip")) return QStringLiteral("DV clip");
            return s.kind;
        }

        bool isPicked(const LevelsScopeData::Section& s) const
        {
            const auto found = picked_.find(s.id);
            if (found != picked_.end()) return found->second;
            return s.kind != QStringLiteral("Apple TV home screen") && s.kind != QStringLiteral("Short DV clip");
        }

        void fillSections()
        {
            filling_ = true;
            sections_->clear();
            for (const auto& s : library_.sections)
            {
                const auto found = names_.find(s.id);
                auto* item = new QTreeWidgetItem(sections_);
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEditable);
                item->setData(0, Qt::UserRole, s.id);
                item->setData(0, Qt::UserRole + 1, s.kind);
                item->setText(0, found != names_.end() ? found->second : s.kind);
                item->setCheckState(0, isPicked(s) ? Qt::Checked : Qt::Unchecked);
                item->setText(1, QDateTime::fromMSecsSinceEpoch(std::llround(s.a * 1000.0)).toString(QStringLiteral("d MMM HH:mm")));
                item->setText(2, QStringLiteral("%1 min").arg(std::lround((s.b - s.a) / 60.0)));
                item->setText(3, s.rangeKnown ? QStringLiteral("%1%2 – %3").arg(s.dolbyVision ? QStringLiteral("DV ") : QString(),
                    LevelsScopeView::Nits(s.black), LevelsScopeView::Nits(s.peak)) : QStringLiteral("not declared"));
                item->setText(4, s.build.isEmpty() ? QStringLiteral("not identified") : s.build);
                item->setToolTip(0, s.files);
            }
            filling_ = false;
            showPicked();
        }

        void showPicked()
        {
            std::vector<const LevelsScopeData::Section*> shown;
            for (const auto& s : library_.sections) if (isPicked(s)) shown.push_back(&s);
            chart_->setSections(shown);
            syncScroll();
        }

        void rebuildLegend()
        {
            while (QLayoutItem* item = legend_->takeAt(0))
            {
                delete item->widget();
                delete item;
            }
            const bool input = settings_.value(QStringLiteral("view"), 1).toInt() == 0;
            for (const auto& t : input ? LevelsScopeView::InputTraces() : LevelsScopeView::InOutTraces())
            {
                // The chips of the Config page's charts (ToneChartPanel): the
                // trace's colour when on, his muted grey when off.
                QColor c = t.color;
                c.setAlpha(255);
                auto* b = new QToolButton;
                b->setText(QStringLiteral("● ") + t.name);
                b->setCheckable(true);
                b->setChecked(chart_->shown(t.key));
                b->setStyleSheet(QStringLiteral(
                    "QToolButton { color: %1; background: #101d2a; border: 1px solid %1; border-radius: 10px; padding: 1px 8px; }"
                    " QToolButton:!checked { color: #63798c; border-color: #2c4659; }").arg(c.name()));
                b->setToolTip(LevelsScopeView::TraceTip(t.key));
                const std::string key = t.key;
                connect(b, &QToolButton::toggled, this, [this, key](bool on) {
                    chart_->setShown(key, on);
                    settings_.setValue(QStringLiteral("shown/") + QString::fromStdString(key), on);
                });
                legend_->addWidget(b);
            }
            updateNote();
        }

        void syncScroll()
        {
            syncing_ = true;
            const int position = chart_->position();
            // Everything shown: no handle at all, not a stray one at the left.
            scroll_->setEnabled(position >= 0);
            scroll_->setRange(0, position >= 0 ? 1000 : 0);
            scroll_->setValue((std::max)(position, 0));
            const int level = chart_->brightnessPosition();
            brightness_->setEnabled(level >= 0);
            brightness_->setRange(0, level >= 0 ? 1000 : 0);
            brightness_->setValue((std::max)(level, 0));
            syncing_ = false;
        }

        void updateNote()
        {
            QString note;
            if (configPath_.isEmpty())
                note = QStringLiteral("Open a VideoProcessor.cfg to choose a DTM profile.");
            else if (!placeError_.isEmpty())
                note = QStringLiteral("The scope needs %1's libplacebo beside this program's folder: %2")
                    .arg(QString::fromWCharArray(ToneCurveRuntime::RendererName()), placeError_);
            else if (settings_.value(QStringLiteral("view"), 1).toInt() == 0)
                note = QStringLiteral("Input scope: what VP logged. Measured values are libplacebo's peak detection; "
                                      "L1 and the mastering range are what the stream declared. Screen lines: the profile's target %1 / %2.")
                    .arg(LevelsScopeView::Short(request_.targetNits), LevelsScopeView::Short(request_.blackNits));
            else
                note = QStringLiteral("In vs out: the profile's curve computed by libplacebo's own functions (%1's "
                                      "%2), through the Config page's tone engine, from what VP logged. "
                                      "Contrast recovery (a detail pass) is not part of the curve. Screen lines: the profile's target %3 / %4.")
                    .arg(QString::fromWCharArray(ToneCurveRuntime::RendererName()), QString::fromWCharArray(ToneCurveRuntime::LibraryFile()),
                        LevelsScopeView::Short(request_.targetNits), LevelsScopeView::Short(request_.blackNits));
            note_->setText(note);
        }

        void loadSections()
        {
            settings_.beginGroup(QStringLiteral("sections"));
            for (const QString& key : settings_.childKeys())
            {
                const QStringList v = settings_.value(key).toStringList();
                const QString id = QString::fromUtf8(QByteArray::fromHex(key.toLatin1()));
                if (v.size() >= 1) picked_[id] = v[0] == QStringLiteral("1");
                if (v.size() >= 2 && !v[1].isEmpty()) names_[id] = v[1];
            }
            settings_.endGroup();
        }

        void saveSections()
        {
            settings_.beginGroup(QStringLiteral("sections"));
            for (const auto& [id, on] : picked_)
            {
                const auto name = names_.find(id);
                settings_.setValue(QString::fromLatin1(id.toUtf8().toHex()),
                    QStringList{ on ? QStringLiteral("1") : QStringLiteral("0"), name != names_.end() ? name->second : QString() });
            }
            settings_.endGroup();
        }

        QSettings settings_;
        ConfigEditorCore::ConfigDocument document_;
        QString configPath_, placeError_;
        ToneCurveProvider::Request request_;
        LevelsScopeData::Library library_;
        bool placed_ = false, filling_ = false, syncing_ = false;
        std::map<QString, bool> picked_;
        std::map<QString, QString> names_;
        QComboBox* profiles_ = nullptr;
        QLabel* configLabel_ = nullptr;
        QLabel* status_ = nullptr;
        QHBoxLayout* logs_ = nullptr;
        QStringList files_;            // the open logs, absolute paths
        QLabel* readout_ = nullptr;
        QLabel* note_ = nullptr;
        QTreeWidget* sections_ = nullptr;
        QHBoxLayout* legend_ = nullptr;
        LevelsScopeView::Chart* chart_ = nullptr;
        QScrollBar* scroll_ = nullptr;
        QScrollBar* brightness_ = nullptr;
        QButtonGroup* viewButtons_ = nullptr;
    };
}
