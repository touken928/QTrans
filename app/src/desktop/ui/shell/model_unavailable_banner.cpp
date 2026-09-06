#include "ui/shell/model_unavailable_banner.h"
#include "ui/shared/theme/theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

ModelUnavailableBanner::ModelUnavailableBanner(QWidget *parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("modelUnavailableBanner"));
    hide();

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(Theme::Space::lg, Theme::Space::sm,
                               Theme::Space::lg, Theme::Space::sm);
    layout->setSpacing(Theme::Space::md);

    // Warning accent dot.
    auto *dot = new QLabel(this);
    dot->setObjectName(QStringLiteral("bannerDot"));
    dot->setFixedSize(Theme::Size::statusDot + 2, Theme::Size::statusDot + 2);
    layout->addWidget(dot, 0, Qt::AlignTop);
    layout->addSpacing(Theme::Space::xs);

    auto *text_col = new QVBoxLayout();
    text_col->setSpacing(Theme::Space::xs);

    title_label_ = new QLabel(this);
    title_label_->setObjectName(QStringLiteral("bannerTitle"));
    text_col->addWidget(title_label_);

    detail_label_ = new QLabel(this);
    detail_label_->setObjectName(QStringLiteral("bannerDetail"));
    detail_label_->setWordWrap(true);
    text_col->addWidget(detail_label_);

    layout->addLayout(text_col, 1);

    auto *dismiss_button = new QPushButton(QStringLiteral("Dismiss"), this);
    dismiss_button->setCursor(Qt::PointingHandCursor);
    layout->addWidget(dismiss_button);

    auto *models_button = new QPushButton(QStringLiteral("Open Models"), this);
    models_button->setCursor(Qt::PointingHandCursor);
    layout->addWidget(models_button);

    primary_button_ = new QPushButton(this);
    primary_button_->setObjectName(QStringLiteral("primaryButton"));
    primary_button_->setCursor(Qt::PointingHandCursor);
    layout->addWidget(primary_button_);

    connect(dismiss_button, &QPushButton::clicked, this, &ModelUnavailableBanner::dismissed);
    connect(models_button, &QPushButton::clicked, this, &ModelUnavailableBanner::modelsRequested);
    connect(primary_button_, &QPushButton::clicked, this, [this]() {
        if (file_missing_) {
            emit downloadRequested();
        } else {
            emit loadRequested();
        }
    });
}

void ModelUnavailableBanner::setState(bool file_missing, const QString &model_name) {
    file_missing_ = file_missing;
    if (file_missing_) {
        title_label_->setText(QStringLiteral("Model not downloaded"));
        detail_label_->setText(
            QStringLiteral("The selected model \u201C%1\u201D is not downloaded yet. "
                           "Download it now or choose another model in Models.")
                .arg(model_name));
        primary_button_->setText(QStringLiteral("Download Model"));
    } else {
        title_label_->setText(QStringLiteral("Model not loaded"));
        detail_label_->setText(
            QStringLiteral("The model \u201C%1\u201D is downloaded but not loaded. "
                           "Load it to start translating, or pick another model in Models.")
                .arg(model_name));
        primary_button_->setText(QStringLiteral("Load Model"));
    }
}
