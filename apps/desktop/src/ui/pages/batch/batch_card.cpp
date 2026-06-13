#include "ui/pages/batch/batch_card.h"
#include "ui/shared/theme/theme.h"
#include "domain/batch/batch_enums.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QVBoxLayout>

namespace {

QString stateLabel(int state) {
    switch (static_cast<BatchEntryState>(state)) {
        case BatchEntryState::Queued:      return QStringLiteral("Queued");
        case BatchEntryState::Processing:  return QStringLiteral("Processing");
        case BatchEntryState::Completed:   return QStringLiteral("Completed");
        case BatchEntryState::Failed:      return QStringLiteral("Failed");
        case BatchEntryState::Cancelled:   return QStringLiteral("Cancelled");
    }
    return QStringLiteral("Unknown");
}

QColor stateColor(int state) {
    switch (static_cast<BatchEntryState>(state)) {
        case BatchEntryState::Queued:      return QColor(Theme::Color::textMuted);
        case BatchEntryState::Processing:  return QColor(Theme::Color::primary);
        case BatchEntryState::Completed:   return QColor(QStringLiteral("#34c759"));
        case BatchEntryState::Failed:      return QColor(Theme::Color::danger);
        case BatchEntryState::Cancelled:   return QColor(Theme::Color::textMuted);
    }
    return QColor(Theme::Color::textMuted);
}

constexpr int kCheckboxSize = 16;

}  // namespace

BatchCard::BatchCard(const QString &entry_id, const QString &file_name,
                     const QString &source_lang, const QString &target_lang,
                     QWidget *parent)
    : QWidget(parent), entry_id_(entry_id) {
    setObjectName(QStringLiteral("batchCard"));
    setFixedHeight(52);
    setCursor(Qt::PointingHandCursor);

    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(Theme::Space::sm, Theme::Space::xs,
                             Theme::Space::md, Theme::Space::xs);
    root->setSpacing(Theme::Space::sm);

    // Spacer at left for checkbox painted area
    root->addSpacing(kCheckboxSize + 6);

    // File name
    file_label_ = new QLabel(file_name, this);
    file_label_->setObjectName(QStringLiteral("cardFile"));
    file_label_->setStyleSheet(QStringLiteral("font-weight: bold; font-size: %1px;")
                                   .arg(Theme::Font::md));
    root->addWidget(file_label_);

    // Source → Target inline
    const QString lang_text = (source_lang.isEmpty() ? QStringLiteral("—") : source_lang)
        + QStringLiteral(" → ")
        + (target_lang.isEmpty() ? QStringLiteral("—") : target_lang);
    lang_label_ = new QLabel(lang_text, this);
    lang_label_->setObjectName(QStringLiteral("cardLangs"));
    lang_label_->setStyleSheet(QStringLiteral("color: %1; font-size: %2px;")
                                   .arg(Theme::Color::textMuted).arg(Theme::Font::xs));
    root->addWidget(lang_label_);

    root->addStretch(1);

    // Progress text
    progress_label_ = new QLabel(QStringLiteral("0 / 0"), this);
    progress_label_->setObjectName(QStringLiteral("cardProgress"));
    progress_label_->setStyleSheet(QStringLiteral("color: %1; font-size: %2px;")
                                       .arg(Theme::Color::textMuted).arg(Theme::Font::xs));
    root->addWidget(progress_label_);

    // Status circle (painted in paintEvent; QLabel kept for geometry reference)
    status_icon_ = new QLabel(this);
    status_icon_->setFixedSize(8, 8);
    status_icon_->setStyleSheet(QStringLiteral("background: transparent;"));
    root->addWidget(status_icon_);

    // Status text
    status_text_ = new QLabel(stateLabel(0), this);
    status_text_->setObjectName(QStringLiteral("cardStatus"));
    status_text_->setStyleSheet(QStringLiteral("color: %1; font-size: %2px;")
                                    .arg(Theme::Color::textMuted).arg(Theme::Font::xs));
    root->addWidget(status_text_);

    // Save indicator
    save_label_ = new QLabel(this);
    save_label_->setObjectName(QStringLiteral("cardSaved"));
    save_label_->setStyleSheet(QStringLiteral(
        "color: #34c759; font-weight: bold; font-size: %1px;").arg(Theme::Font::lg));
    save_label_->setVisible(false);
    root->addWidget(save_label_);

    // Pulse timer for Processing animation
    pulse_timer_ = new QTimer(this);
    pulse_timer_->setInterval(600);
    connect(pulse_timer_, &QTimer::timeout, this, [this]() {
        pulse_step_ = (pulse_step_ + 1) % 4;
        update();
    });
}

void BatchCard::setSelected(bool selected) {
    if (selected_ == selected) return;
    selected_ = selected;
    update();
}

void BatchCard::setState(int state) {
    state_ = state;
    status_text_->setText(stateLabel(state));
    status_text_->setStyleSheet(QStringLiteral(
        "color: %1; font-size: %2px;").arg(stateColor(state).name()).arg(Theme::Font::xs));

    // Manage pulse timer for Processing state.
    if (static_cast<BatchEntryState>(state) == BatchEntryState::Processing) {
        pulse_step_ = 0;
        pulse_timer_->start();
    } else {
        pulse_timer_->stop();
    }
    update();
}

void BatchCard::setProgress(int completed, int total) {
    completed_ = completed;
    total_ = total;
    progress_label_->setText(QStringLiteral("%1 / %2").arg(completed).arg(total));
}

void BatchCard::setSaved(bool saved, const QString &output_path) {
    saved_ = saved;
    save_label_->setVisible(saved);
    save_label_->setText(QStringLiteral("✓"));
    if (!output_path.isEmpty()) file_path_ = output_path;
}

void BatchCard::mousePressEvent(QMouseEvent *event) {
    Q_UNUSED(event);
    emit clicked(entry_id_);
}

QColor BatchCard::pulseColor() const {
    QColor c = stateColor(state_);
    c.setAlpha(pulseAlpha());
    return c;
}

int BatchCard::pulseAlpha() const {
    if (static_cast<BatchEntryState>(state_) != BatchEntryState::Processing)
        return 255;
    // 4-step pulse: 255, 160, 80, 160
    switch (pulse_step_) {
        case 0: return 255;
        case 1: return 160;
        case 2: return 80;
        case 3: return 160;
    }
    return 255;
}

void BatchCard::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // White card background with rounded corners
    p.setBrush(QColor(Theme::Color::surface));
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(rect(), Theme::Radius::sm, Theme::Radius::sm);

    // Checkbox square at left margin
    const int cx = Theme::Space::sm;
    const int cy = (height() - kCheckboxSize) / 2;
    const QRect cb(cx, cy, kCheckboxSize, kCheckboxSize);

    p.setPen(QPen(selected_ ? QColor(Theme::Color::primary)
                            : QColor(Theme::Color::border), 1.5));
    p.setBrush(selected_ ? QColor(Theme::Color::primary) : QColor(Qt::transparent));
    p.drawRoundedRect(cb, 3, 3);

    if (selected_) {
        // Checkmark
        p.setPen(QPen(Qt::white, 2));
        const int m = 4;
        p.drawLine(cb.left() + m, cb.center().y(),
                   cb.center().x(), cb.bottom() - m);
        p.drawLine(cb.center().x(), cb.bottom() - m,
                   cb.right() - m, cb.top() + m);
    }

    // Status circle — pulse color for Processing
    if (status_icon_) {
        const QRect cir = status_icon_->geometry();
        QColor sc;
        if (static_cast<BatchEntryState>(state_) == BatchEntryState::Processing) {
            sc = pulseColor();
        } else {
            sc = stateColor(state_);
        }
        p.setBrush(sc);
        p.setPen(Qt::NoPen);
        p.drawEllipse(cir);
    }
}

void BatchCard::updateStyle() {
    update();
}
