#include "app/ui/download_progress_panel.h"

#include <QFont>
#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>

namespace {

QString formatBytes(double bytes) {
    if (bytes < 1024.0) {
        return QStringLiteral("%1 B/s").arg(static_cast<int>(bytes));
    }
    if (bytes < 1024.0 * 1024.0) {
        return QStringLiteral("%1 KB/s").arg(bytes / 1024.0, 0, 'f', 1);
    }
    return QStringLiteral("%1 MB/s").arg(bytes / (1024.0 * 1024.0), 0, 'f', 2);
}

QString formatSize(qint64 bytes) {
    if (bytes < 1024) {
        return QStringLiteral("%1 B").arg(bytes);
    }
    if (bytes < 1024 * 1024) {
        return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    }
    if (bytes < 1024LL * 1024 * 1024) {
        return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 2);
    }
    return QStringLiteral("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}

QString formatEta(double seconds) {
    if (seconds < 0.0) {
        return QStringLiteral("--:--");
    }

    const int total = static_cast<int>(seconds);
    const int minutes = total / 60;
    const int secs = total % 60;
    return QStringLiteral("%1:%2")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(secs, 2, 10, QChar('0'));
}

} // namespace

DownloadProgressPanel::DownloadProgressPanel(QWidget * parent)
    : QWidget(parent) {
    auto * layout = new QVBoxLayout(this);
    layout->setSpacing(12);

    auto * title = new QLabel(QStringLiteral("Downloading Model"), this);
    QFont title_font = title->font();
    title_font.setBold(true);
    title_font.setPointSize(title_font.pointSize() + 2);
    title->setFont(title_font);
    layout->addWidget(title);

    status_label_ = new QLabel(QStringLiteral("Preparing download..."), this);
    status_label_->setWordWrap(true);
    layout->addWidget(status_label_);

    progress_bar_ = new QProgressBar(this);
    progress_bar_->setRange(0, 100);
    progress_bar_->setValue(0);
    layout->addWidget(progress_bar_);

    speed_label_ = new QLabel(QStringLiteral("Speed: --"), this);
    speed_label_->setObjectName(QStringLiteral("mutedLabel"));
    layout->addWidget(speed_label_);

    eta_label_ = new QLabel(QStringLiteral("Time left: --:--"), this);
    eta_label_->setObjectName(QStringLiteral("mutedLabel"));
    layout->addWidget(eta_label_);
}

void DownloadProgressPanel::setProgress(
    qint64 downloaded,
    qint64 total,
    double speed_bps,
    double eta_seconds) {
    if (total > 0) {
        const int percent = static_cast<int>(downloaded * 100 / total);
        progress_bar_->setRange(0, 100);
        progress_bar_->setValue(percent);
        status_label_->setText(
            QStringLiteral("%1 / %2")
                .arg(formatSize(downloaded))
                .arg(formatSize(total)));
    } else {
        progress_bar_->setRange(0, 0);
        status_label_->setText(QStringLiteral("Downloaded %1").arg(formatSize(downloaded)));
    }

    speed_label_->setText(QStringLiteral("Speed: %1").arg(formatBytes(speed_bps)));
    eta_label_->setText(QStringLiteral("Time left: %1").arg(formatEta(eta_seconds)));
}

void DownloadProgressPanel::setFailure() {
    status_label_->setText(QStringLiteral("Download failed. Check your network and try again."));
    progress_bar_->setRange(0, 100);
}

void DownloadProgressPanel::setLoading() {
    status_label_->setText(QStringLiteral("Download complete. Loading model..."));
    progress_bar_->setRange(0, 100);
    progress_bar_->setValue(100);
}
