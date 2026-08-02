#include "ui/shell/shell_status_bar.h"
#include "ui/shared/theme/theme.h"

#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QResizeEvent>
#include <QStyle>

namespace {

// Adaptive byte size, e.g. "1.2 KB", "12.3 MB", "1.2 GB".
QString formatBytes(qint64 bytes) {
    if (bytes < 0) {
        return {};
    }
    double value = static_cast<double>(bytes);
    static const char *kUnits[] = {"B", "KB", "MB", "GB", "TB"};
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    if (unit == 0) {
        return QStringLiteral("%1 B").arg(bytes);
    }
    return value >= 100.0
               ? QStringLiteral("%1 %2").arg(value, 0, 'f', 0).arg(QString::fromLatin1(kUnits[unit]))
               : QStringLiteral("%1 %2").arg(value, 0, 'f', 1).arg(QString::fromLatin1(kUnits[unit]));
}

// Adaptive speed, e.g. "512 KB/s", "12.3 MB/s".
QString formatSpeed(double speed_bps) {
    if (speed_bps <= 0.0) {
        return {};
    }
    double value = speed_bps;
    static const char *kUnits[] = {"B/s", "KB/s", "MB/s", "GB/s"};
    int unit = 0;
    while (value >= 1024.0 && unit < 3) {
        value /= 1024.0;
        ++unit;
    }
    return value >= 100.0
               ? QStringLiteral("%1 %2").arg(value, 0, 'f', 0).arg(QString::fromLatin1(kUnits[unit]))
               : QStringLiteral("%1 %2").arg(value, 0, 'f', 1).arg(QString::fromLatin1(kUnits[unit]));
}

// Compact remaining time: "12s", "4m", "1m 24s", "1h 5m". Empty when the
// estimate is missing or implausible (stalled or enormous).
QString formatEta(double eta_seconds) {
    if (eta_seconds <= 0.0 || eta_seconds > 7.0 * 24.0 * 3600.0) {
        return {};
    }
    const qint64 secs = static_cast<qint64>(eta_seconds + 0.5);
    if (secs < 60) {
        return QStringLiteral("%1s").arg(secs);
    }
    const qint64 mins = secs / 60;
    if (mins < 60) {
        const qint64 rem = secs % 60;
        return rem == 0 ? QStringLiteral("%1m").arg(mins)
                        : QStringLiteral("%1m %2s").arg(mins).arg(rem);
    }
    const qint64 hours = mins / 60;
    const qint64 rem_mins = mins % 60;
    return rem_mins == 0 ? QStringLiteral("%1h").arg(hours)
                         : QStringLiteral("%1h %2m").arg(hours).arg(rem_mins);
}

// Elides `text` to the label's current width; before the first layout pass
// (width <= 0) the raw text is kept so the bar never shows an empty value.
QString elidedText(const QLabel *label, const QString &text) {
    if (label->width() <= 0) {
        return text;
    }
    const QFontMetrics metrics(label->font());
    return metrics.elidedText(text, Qt::ElideRight, label->width() - 2);
}

// 1px vertical divider between status groups; styled via statusBarSeparator.
void addSeparator(QHBoxLayout *layout) {
    auto *separator = new QLabel();
    separator->setObjectName(QStringLiteral("statusBarSeparator"));
    separator->setFixedSize(1, Theme::Size::statusBarSeparatorHeight);
    layout->addWidget(separator);
}

}  // namespace

ShellStatusBar::ShellStatusBar(QWidget *parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("shellStatusBar"));
    setFixedHeight(Theme::Size::statusBarHeight);
    // Plain QWidget subclasses do not paint stylesheet backgrounds/borders
    // unless this attribute is set — without it the band was transparent
    // and merged into the page. This makes the band surface + boundary real.
    setAttribute(Qt::WA_StyledBackground, true);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // 1px highlight hairline under the 2px top border: the classic recessed
    // status-bar groove (QSS has no reliable shadow support).
    auto *top_highlight = new QLabel(this);
    top_highlight->setObjectName(QStringLiteral("statusBarTopHighlight"));
    top_highlight->setFixedHeight(1);
    root->addWidget(top_highlight);

    auto *row = new QWidget(this);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(Theme::Space::lg, 0, Theme::Space::lg, 0);
    layout->setSpacing(Theme::Space::sm);

    // ── Activity chip: coloured dot + text (primary status) ──────────
    chip_dot_ = new QLabel(this);
    chip_dot_->setObjectName(QStringLiteral("statusChipDot"));
    chip_dot_->setFixedSize(Theme::Size::statusDot, Theme::Size::statusDot);
    layout->addWidget(chip_dot_);

    chip_text_ = new QLabel(this);
    chip_text_->setObjectName(QStringLiteral("statusChipText"));
    chip_text_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    // Fixed-width activity slot: the chip text elides (tooltip carries the
    // full message) so Ready/Loading/Translating never shift the model and
    // backend groups that follow.
    chip_text_->setFixedWidth(Theme::Size::statusBarActivityWidth);
    layout->addWidget(chip_text_);

    addSeparator(layout);

    // ── Loaded model: caption + bounded elided value ──────────────────
    auto *loaded_caption = new QLabel(QStringLiteral("Loaded"), this);
    loaded_caption->setObjectName(QStringLiteral("statusBarCaption"));
    loaded_caption->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(loaded_caption);

    loaded_value_ = new QLabel(this);
    loaded_value_->setObjectName(QStringLiteral("statusBarModelValue"));
    loaded_value_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    // Bounded slot: takes whatever the layout can spare between a floor and
    // a cap and elides (tooltip carries the full name), so long model names
    // never push the backend/download groups and short names never leave a
    // drifting gap.
    loaded_value_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    loaded_value_->setMinimumWidth(Theme::Size::statusBarLoadedMinWidth);
    loaded_value_->setMaximumWidth(Theme::Size::statusBarLoadedMaxWidth);
    layout->addWidget(loaded_value_, 1);

    addSeparator(layout);

    // ── Backend usage: caption + stable-width elided value ────────────
    auto *backend_caption = new QLabel(QStringLiteral("Backend"), this);
    backend_caption->setObjectName(QStringLiteral("statusBarCaption"));
    backend_caption->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(backend_caption);

    backend_value_ = new QLabel(this);
    backend_value_->setObjectName(QStringLiteral("statusBarModelValue"));
    backend_value_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    // Fixed width: backend labels change at load time and must not shift the
    // rest of the bar; long labels elide with the full text in the tooltip.
    backend_value_->setFixedWidth(Theme::Size::statusBarBackendWidth);
    layout->addWidget(backend_value_);

    layout->addStretch(1);

    // ── Download metrics: slim progress + speed/ETA (transient) ───────
    download_progress_ = new QProgressBar(this);
    download_progress_->setObjectName(QStringLiteral("statusBarProgress"));
    download_progress_->setRange(0, 1000);
    download_progress_->setValue(0);
    download_progress_->setTextVisible(false);
    download_progress_->setFixedWidth(Theme::Size::statusBarProgressWidth);
    download_progress_->setAccessibleName(QStringLiteral("Model download progress"));
    download_progress_->setVisible(false);
    layout->addWidget(download_progress_);

    speed_label_ = new QLabel(this);
    speed_label_->setObjectName(QStringLiteral("statusBarMuted"));
    speed_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    speed_label_->setMaximumWidth(Theme::Size::statusBarMetricsMaxWidth);
    speed_label_->setVisible(false);
    layout->addWidget(speed_label_);

    root->addWidget(row, 1);

    setActivity(Activity::Idle, QStringLiteral("No model loaded"));
    setLoadedModel({});
    setBackend({});
}

void ShellStatusBar::setLoadedModel(const QString &display_name) {
    loaded_raw_ = display_name.isEmpty() ? QStringLiteral("\u2014") : display_name;
    loaded_value_->setToolTip(
        display_name.isEmpty()
            ? QStringLiteral("No model is loaded")
            : QStringLiteral("Loaded model: %1").arg(display_name));
    refreshElisions();
}

void ShellStatusBar::setBackend(const QString &label) {
    backend_raw_ = label.isEmpty() ? QStringLiteral("\u2014") : label;
    backend_value_->setToolTip(
        label.isEmpty()
            ? QStringLiteral("No backend is in use")
            : QStringLiteral("Backend usage: %1").arg(label));
    refreshElisions();
}

void ShellStatusBar::setActivity(Activity activity, const QString &text) {
    activity_ = activity;
    activity_text_ = text;
    refreshChipText();
}

void ShellStatusBar::setDownloadProgress(qint64 downloaded, qint64 total,
                                         double speed_bps, double eta_seconds) {
    downloaded_ = downloaded;
    total_ = total;
    speed_bps_ = speed_bps;
    has_download_metrics_ = (total > 0);

    download_progress_->setVisible(has_download_metrics_);
    if (has_download_metrics_) {
        // Bytes are qint64: project a bounded 0..1000 permille in long
        // double so no int arithmetic can overflow, then clamp.
        long double permille = static_cast<long double>(downloaded_) * 1000.0L /
                               static_cast<long double>(total_);
        if (permille < 0.0L) {
            permille = 0.0L;
        } else if (permille > 1000.0L) {
            permille = 1000.0L;
        }
        download_progress_->setValue(static_cast<int>(permille));

        // Speed with a compact ETA suffix, e.g. "12.3 MB/s · 1m 24s".
        speed_raw_ = formatSpeed(speed_bps_);
        const QString eta = formatEta(eta_seconds);
        if (!eta.isEmpty()) {
            speed_raw_ = speed_raw_.isEmpty() ? eta
                                              : speed_raw_ + QStringLiteral(" \u00B7 ") + eta;
        }
        speed_label_->setVisible(!speed_raw_.isEmpty());

        // Full state on the progress surface (the bar itself is bare).
        const int percent = static_cast<int>(permille / 10.0L + 0.5L);
        const QString detail = QStringLiteral("%1 of %2 (%3%)")
                                   .arg(formatBytes(downloaded_), formatBytes(total_))
                                   .arg(percent);
        download_progress_->setToolTip(
            speed_raw_.isEmpty() ? QStringLiteral("Downloading model: %1").arg(detail)
                                 : QStringLiteral("Downloading model: %1 \u00B7 %2")
                                       .arg(detail, speed_raw_));
        download_progress_->setAccessibleName(
            QStringLiteral("Model download: %1").arg(detail));
    } else {
        speed_raw_.clear();
        speed_label_->clear();
        speed_label_->setVisible(false);
        download_progress_->setToolTip({});
        download_progress_->setAccessibleName(QStringLiteral("Model download progress"));
    }
    refreshElisions();
    refreshChipText();
}

void ShellStatusBar::refreshChipText() {
    chip_text_->setText(activity_text_);
    chip_text_->setToolTip(activity_text_);

    const char *state = "idle";
    switch (activity_) {
        case Activity::Loading:
        case Activity::Translating:
            state = "loading";
            break;
        case Activity::Ready:
            state = "ready";
            break;
        case Activity::Paused:
            state = "paused";
            break;
        case Activity::Downloading:
            state = "downloading";
            break;
        case Activity::Failed:
            state = "failed";
            break;
        case Activity::Idle:
            state = "idle";
            break;
    }
    chip_dot_->setProperty("chipState", QLatin1String(state));
    chip_dot_->style()->unpolish(chip_dot_);
    chip_dot_->style()->polish(chip_dot_);
    chip_text_->setAccessibleName(QStringLiteral("Status: %1").arg(activity_text_));
    // Elide the freshly set text to the current label widths.
    refreshElisions();
}

void ShellStatusBar::refreshElisions() {
    // Chip text is elided to its current width too, so long activity
    // messages compress instead of pushing the model/backend groups.
    chip_text_->setText(elidedText(chip_text_, activity_text_));
    loaded_value_->setText(elidedText(loaded_value_, loaded_raw_));
    backend_value_->setText(elidedText(backend_value_, backend_raw_));
    speed_label_->setText(elidedText(speed_label_, speed_raw_));
}

void ShellStatusBar::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    refreshElisions();
}
