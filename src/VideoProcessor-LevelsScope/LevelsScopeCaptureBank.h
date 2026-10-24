#pragma once
#include "LevelsScopeCapture.h"
#include "LevelsScopeReview.h"
#include "LevelsScopeThumbnail.h"
#include <functional>
#include <memory>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

// The captures on this PC (VIEWING-PLAN "Bank on the second PC"): each with its
// preview, newest first. Review (build 2) chooses a still from here.
namespace LevelsScopeCaptureBank
{
    // One line of what a capture is, from its sidecar.
    inline QString Caption(const QJsonObject& root)
    {
        const QJsonObject identity = root.value(QStringLiteral("identity")).toObject();
        const QJsonObject frame = root.value(QStringLiteral("frame")).toObject();
        const QJsonObject device = root.value(QStringLiteral("device_state")).toObject();
        const QJsonObject vp = root.value(QStringLiteral("vp_state")).toObject();
        QString time = identity.value(QStringLiteral("capture_time")).toString();
        time.replace(u'T', u' ');
        const QString kind = vp.value(QStringLiteral("dolby_vision_tunnel")).toBool() ?
            QStringLiteral("Dolby Vision") : vp.value(QStringLiteral("eotf")).toString();
        return QStringLiteral("%1\n%2×%3 %4, %5").arg(time)
            .arg(frame.value(QStringLiteral("width")).toInt())
            .arg(frame.value(QStringLiteral("height")).toInt())
            .arg(LevelsScopeThumbnail::Code(device.value(QStringLiteral("encoding")).toString()), kind);
    }

    // What VP measured on the frame, for the tooltip.
    inline QString Details(const QJsonObject& root, const QString& pixelPath)
    {
        const QJsonObject renderer = root.value(QStringLiteral("renderer")).toObject();
        const QJsonObject profiles = root.value(QStringLiteral("profiles")).toObject();
        QStringList lines{ QDir::toNativeSeparators(pixelPath) };
        if (!renderer.value(QStringLiteral("available")).toBool())
            lines << QStringLiteral("No VP measurements: %1").arg(renderer.value(QStringLiteral("reason")).toString());
        else
        {
            const QJsonObject detection = renderer.value(QStringLiteral("detection")).toObject();
            const QJsonObject scene = renderer.value(QStringLiteral("scene")).toObject();
            if (detection.value(QStringLiteral("valid")).toBool())
                lines << QStringLiteral("Detected peak %1 nits, average %2 nits")
                    .arg(detection.value(QStringLiteral("peak_nits")).toDouble(), 0, 'f', 1)
                    .arg(detection.value(QStringLiteral("avg_pq_nits")).toDouble(), 0, 'f', 2);
            if (scene.value(QStringLiteral("valid")).toBool())
                lines << QStringLiteral("Scene peak %1 nits, average %2 nits")
                    .arg(scene.value(QStringLiteral("peak_nits")).toDouble(), 0, 'f', 1)
                    .arg(scene.value(QStringLiteral("avg_nits")).toDouble(), 0, 'f', 2);
        }
        for (auto it = profiles.begin(); it != profiles.end(); ++it)
            lines << QStringLiteral("%1: %2").arg(it.key(), it.value().toString());
        const QString thumbnailKind = LevelsScopeThumbnail::Kind(
            LevelsScopeThumbnail::Code(root.value(QStringLiteral("device_state")).toObject()
                .value(QStringLiteral("encoding")).toString()),
            root.value(QStringLiteral("vp_state")).toObject().value(QStringLiteral("dolby_vision_tunnel")).toBool());
        lines << (thumbnailKind.isEmpty() ? QStringLiteral("No preview for this encoding") :
            QStringLiteral("Preview: %1, for picking, not judging").arg(thumbnailKind));
        return lines.join(u'\n');
    }

    // The rig the scope is connected to: false when none.
    using RigTarget = std::function<bool(QString& host, quint16& port, QString& name)>;

    class Window : public QDialog
    {
    public:
        Window(QWidget* parent, RigTarget rig) : QDialog(parent), rig_(std::move(rig))
        {
            setWindowTitle(QStringLiteral("Captures"));
            resize(1100, 700);
            auto* layout = new QVBoxLayout(this);
            list_ = new QListWidget;
            list_->setViewMode(QListView::IconMode);
            list_->setIconSize(QSize(LevelsScopeThumbnail::Width, LevelsScopeThumbnail::Width * 9 / 16));
            list_->setResizeMode(QListView::Adjust);
            list_->setMovement(QListView::Static);
            list_->setSpacing(8);
            list_->setWordWrap(true);
            layout->addWidget(list_, 1);
            empty_ = new QLabel;
            empty_->setProperty("help", true);
            layout->addWidget(empty_);
            status_ = new QLabel;
            status_->setWordWrap(true);
            layout->addWidget(status_);
            auto* buttons = new QHBoxLayout;
            show_ = new QPushButton(QStringLiteral("Show on rig"));
            show_->setToolTip(QStringLiteral("Send the chosen still to the rig and show it in place of the live picture, through the live profile"));
            live_ = new QPushButton(QStringLiteral("Back to live"));
            live_->setToolTip(QStringLiteral("Show the live source on the rig again"));
            auto* open = new QPushButton(QStringLiteral("Open captures folder"));
            auto* close = new QPushButton(QStringLiteral("Close"));
            buttons->addWidget(show_);
            buttons->addWidget(live_);
            buttons->addStretch(1);
            buttons->addWidget(open);
            buttons->addWidget(close);
            layout->addLayout(buttons);
            connect(show_, &QPushButton::clicked, this, [this] { showOnRig(); });
            connect(live_, &QPushButton::clicked, this, [this] { backToLive(); });
            connect(list_, &QListWidget::itemDoubleClicked, this, [this] { showOnRig(); });
            connect(open, &QPushButton::clicked, this, [] {
                QDir().mkpath(LevelsScopeCapture::Folder());
                QDesktopServices::openUrl(QUrl::fromLocalFile(LevelsScopeCapture::Folder()));
            });
            connect(close, &QPushButton::clicked, this, &QDialog::close);
            reload();
        }

        // Every capture in the folder, newest first (names sort by log time).
        void reload()
        {
            list_->clear();
            QDir folder(LevelsScopeCapture::Folder());
            const QStringList sidecars = folder.entryList({ QStringLiteral("capture-*.json") },
                QDir::Files, QDir::Name | QDir::Reversed);
            for (const QString& name : sidecars)
            {
                QFile file(folder.absoluteFilePath(name));
                if (!file.open(QIODevice::ReadOnly)) continue;
                const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
                if (!document.isObject()) continue;
                const QJsonObject root = document.object();
                const QString pixelPath = folder.absoluteFilePath(QFileInfo(name).completeBaseName() + QStringLiteral(".raw"));
                if (!QFileInfo::exists(pixelPath)) continue;
                auto* item = new QListWidgetItem(Thumbnail(pixelPath, root), Caption(root));
                item->setToolTip(Details(root, pixelPath));
                item->setData(Qt::UserRole, pixelPath);
                item->setData(Qt::UserRole + 1, root);
                list_->addItem(item);
            }
            empty_->setText(list_->count() == 0 ?
                QStringLiteral("No captures yet. Connect a rig and press Capture frame.") :
                QStringLiteral("%1 captures in %2").arg(list_->count())
                    .arg(QDir::toNativeSeparators(LevelsScopeCapture::Folder())));
        }

    private:
        // Network work off the window thread, one at a time.
        void runOnRig(const QString& busy, std::function<void(const QString&, quint16, std::string&)> work,
            std::function<QString()> done)
        {
            QString host, name;
            quint16 port = 0;
            if (worker_) return;
            if (!rig_ || !rig_(host, port, name))
            {
                status_->setText(QStringLiteral("Connect a rig in the scope first."));
                return;
            }
            status_->setText(busy.arg(name));
            auto error = std::make_shared<std::string>();
            worker_ = QThread::create([work, host, port, error] { work(host, port, *error); });
            worker_->setParent(this);
            QThread* worker = worker_;
            connect(worker, &QThread::finished, this, [this, worker, name, error, done] {
                worker_ = nullptr;
                worker->deleteLater();
                show_->setEnabled(true);
                live_->setEnabled(true);
                status_->setText(error->empty() ? done().arg(name) :
                    QStringLiteral("%1: %2").arg(name, QString::fromStdString(*error)));
            });
            show_->setEnabled(false);
            live_->setEnabled(false);
            worker->start();
        }

        void showOnRig()
        {
            QListWidgetItem* item = list_->currentItem();
            if (!item)
            {
                status_->setText(QStringLiteral("Choose a still first."));
                return;
            }
            const QString pixelPath = item->data(Qt::UserRole).toString();
            const QJsonObject root = item->data(Qt::UserRole + 1).toJsonObject();
            const QString caption = item->text().section(u'\n', 0, 0);
            runOnRig(QStringLiteral("Sending the still to %1…"),
                [pixelPath, root](const QString& host, quint16 port, std::string& error) {
                    LevelsScopeReview::Show(host, port, pixelPath, root, error);
                },
                [caption] { return QStringLiteral("%1 shows the still from ") + caption + QStringLiteral("."); });
        }

        void backToLive()
        {
            runOnRig(QStringLiteral("Returning %1 to live…"),
                [](const QString& host, quint16 port, std::string& error) {
                    LevelsScopeReview::Live(host, port, error);
                },
                [] { return QStringLiteral("%1 shows the live source."); });
        }

        // The saved preview, or one made now from the pixels (and saved).
        static QIcon Thumbnail(const QString& pixelPath, const QJsonObject& root)
        {
            const QString path = LevelsScopeCapture::ThumbnailPath(pixelPath);
            QPixmap pixmap(path);
            if (pixmap.isNull())
            {
                QFile pixels(pixelPath);
                if (pixels.open(QIODevice::ReadOnly))
                {
                    const QImage image = LevelsScopeThumbnail::Make(pixels.readAll(), root);
                    if (!image.isNull())
                    {
                        image.save(path, "PNG");
                        pixmap = QPixmap::fromImage(image);
                    }
                }
            }
            return pixmap.isNull() ? QIcon() : QIcon(pixmap);
        }

        RigTarget rig_;
        QListWidget* list_ = nullptr;
        QLabel* empty_ = nullptr;
        QLabel* status_ = nullptr;
        QPushButton* show_ = nullptr;
        QPushButton* live_ = nullptr;
        QThread* worker_ = nullptr;
    };
}
