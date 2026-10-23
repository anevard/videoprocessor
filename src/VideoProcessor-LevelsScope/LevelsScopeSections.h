#pragma once
#include "LevelsScopeData.h"
#include "LevelsScopeSettings.h"
#include <ChartStyle.h>
#include <QAbstractItemView>
#include <QDateTime>
#include <QHeaderView>
#include <QJsonArray>
#include <QStringList>
#include <QTreeWidget>
#include <algorithm>
#include <cmath>
#include <map>
#include <utility>
#include <vector>

// The section picker: which sections are ticked, and the names the operator
// gave them. The panel places its table and redraws when a tick changes.
namespace LevelsScopeSections
{
    class Picker
    {
    public:
        // Sections: tick to show, double-click a name to rename.
        static QTreeWidget* MakeTable()
        {
            auto* sections = new QTreeWidget;
            sections->setColumnCount(5);
            sections->setHeaderLabels({ QStringLiteral("Section"), QStringLiteral("Start"), QStringLiteral("Length"),
                QStringLiteral("Mastering (metadata)"), QStringLiteral("Build") });
            sections->setRootIsDecorated(false);
            sections->setEditTriggers(QAbstractItemView::NoEditTriggers);
            sections->setMaximumHeight(150);
            sections->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
            return sections;
        }

        // True while the table is being filled: its item changes are not edits.
        bool filling() const { return filling_; }

        // A tick or a rename in the section column.
        void edited(QTreeWidgetItem* item, int column)
        {
            const QString id = item->data(0, Qt::UserRole).toString();
            if (column == 0)
            {
                picked_[id] = item->checkState(0) == Qt::Checked;
                const QString text = item->text(0).trimmed();
                const QString kind = item->data(0, Qt::UserRole + 1).toString();
                if (text.isEmpty() || text == kind) names_.erase(id); else names_[id] = text;
                save();
            }
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

        void fill(QTreeWidget* sections, const LevelsScopeData::Library& library)
        {
            filling_ = true;
            sections->clear();
            for (const auto& s : library.sections)
            {
                const auto found = names_.find(s.id);
                auto* item = new QTreeWidgetItem(sections);
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEditable);
                item->setData(0, Qt::UserRole, s.id);
                item->setData(0, Qt::UserRole + 1, s.kind);
                item->setText(0, found != names_.end() ? found->second : s.kind);
                item->setCheckState(0, isPicked(s) ? Qt::Checked : Qt::Unchecked);
                item->setText(1, QDateTime::fromMSecsSinceEpoch(std::llround(s.a * 1000.0)).toString(QStringLiteral("d MMM HH:mm")));
                item->setText(2, QStringLiteral("%1 min").arg(std::lround((s.b - s.a) / 60.0)));
                item->setText(3, s.rangeKnown ? QStringLiteral("%1%2 – %3").arg(s.dolbyVision ? QStringLiteral("DV ") : QString(),
                    ChartStyle::Nits(s.black), ChartStyle::Nits(s.peak)) : QStringLiteral("not declared"));
                item->setText(4, s.build.isEmpty() ? QStringLiteral("not identified") : s.build);
                item->setToolTip(0, s.files);
            }
            filling_ = false;
        }

        // sections.json in the settings folder; none (a snapshot run): nothing read.
        void load(const QString& folder)
        {
            path_ = folder.isEmpty() ? QString() : QDir(folder).absoluteFilePath(QStringLiteral("sections.json"));
            if (path_.isEmpty()) return;
            for (const QJsonValue& value : LevelsScopeSettings::ReadJson(path_).value(QStringLiteral("sections")).toArray())
            {
                const QJsonObject s = value.toObject();
                const QString id = s.value(QStringLiteral("id")).toString();
                if (id.isEmpty()) continue;
                picked_[id] = s.value(QStringLiteral("ticked")).toBool();
                const QString name = s.value(QStringLiteral("name")).toString();
                if (!name.isEmpty()) names_[id] = name;
            }
        }

        // Every ticked or renamed section, oldest first. The id is the match key
        // (start seconds|DV|master black|master peak); the other fields repeat
        // it readably, so a name can be looked up by hand.
        void save() const
        {
            if (path_.isEmpty()) return;
            std::vector<std::pair<double, QJsonObject>> rows;
            for (const auto& [id, on] : picked_)
            {
                const QStringList parts = id.split(u'|');
                const double start = parts.value(0).toDouble();
                const auto name = names_.find(id);
                rows.push_back({ start, QJsonObject{
                    { QStringLiteral("id"), id },
                    { QStringLiteral("start"), QDateTime::fromSecsSinceEpoch(static_cast<qint64>(std::floor(start)))
                        .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) },
                    { QStringLiteral("dolbyVision"), parts.value(1) == QStringLiteral("1") },
                    { QStringLiteral("masterBlack"), parts.value(2).toDouble() },
                    { QStringLiteral("masterPeak"), parts.value(3).toDouble() },
                    { QStringLiteral("ticked"), on },
                    { QStringLiteral("name"), name != names_.end() ? name->second : QString() } } });
            }
            std::stable_sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            QJsonArray sections;
            for (const auto& row : rows) sections.append(row.second);
            LevelsScopeSettings::WriteJson(path_, QJsonObject{ { QStringLiteral("sections"), sections } });
        }

    private:
        QString path_;
        bool filling_ = false;
        std::map<QString, bool> picked_;
        std::map<QString, QString> names_;
    };
}
