#pragma once
#include "LevelsScopeData.h"
#include <ChartStyle.h>
#include <QAbstractItemView>
#include <QByteArray>
#include <QDateTime>
#include <QHeaderView>
#include <QSettings>
#include <QStringList>
#include <QTreeWidget>
#include <cmath>
#include <map>

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
        void edited(QTreeWidgetItem* item, int column, QSettings& settings)
        {
            const QString id = item->data(0, Qt::UserRole).toString();
            if (column == 0)
            {
                picked_[id] = item->checkState(0) == Qt::Checked;
                const QString text = item->text(0).trimmed();
                const QString kind = item->data(0, Qt::UserRole + 1).toString();
                if (text.isEmpty() || text == kind) names_.erase(id); else names_[id] = text;
                save(settings);
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

        void load(QSettings& settings)
        {
            settings.beginGroup(QStringLiteral("sections"));
            for (const QString& key : settings.childKeys())
            {
                const QStringList v = settings.value(key).toStringList();
                const QString id = QString::fromUtf8(QByteArray::fromHex(key.toLatin1()));
                if (v.size() >= 1) picked_[id] = v[0] == QStringLiteral("1");
                if (v.size() >= 2 && !v[1].isEmpty()) names_[id] = v[1];
            }
            settings.endGroup();
        }

        void save(QSettings& settings) const
        {
            settings.beginGroup(QStringLiteral("sections"));
            for (const auto& [id, on] : picked_)
            {
                const auto name = names_.find(id);
                settings.setValue(QString::fromLatin1(id.toUtf8().toHex()),
                    QStringList{ on ? QStringLiteral("1") : QStringLiteral("0"), name != names_.end() ? name->second : QString() });
            }
            settings.endGroup();
        }

    private:
        bool filling_ = false;
        std::map<QString, bool> picked_;
        std::map<QString, QString> names_;
    };
}
