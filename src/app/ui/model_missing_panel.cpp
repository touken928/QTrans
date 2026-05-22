#include "app/ui/model_missing_panel.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

ModelMissingPanel::ModelMissingPanel(
    const QString & mode_label,
    const QString & model_path,
    QWidget * parent)
    : QWidget(parent) {
    auto * layout = new QVBoxLayout(this);
    layout->setSpacing(12);

    auto * title = new QLabel(QStringLiteral("Model Not Found"), this);
    QFont title_font = title->font();
    title_font.setBold(true);
    title_font.setPointSize(title_font.pointSize() + 2);
    title->setFont(title_font);
    layout->addWidget(title);

    const QString message = QStringLiteral(
                                "The model file was not found in %1 mode:\n%2\n\nDownload and load it now?")
                                .arg(mode_label)
                                .arg(model_path);
    auto * body = new QLabel(message, this);
    body->setWordWrap(true);
    body->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(body);

    auto * buttons = new QHBoxLayout();
    buttons->addStretch(1);

    auto * later_button = new QPushButton(QStringLiteral("Not Now"), this);
    auto * download_button = new QPushButton(QStringLiteral("Download"), this);
    download_button->setObjectName(QStringLiteral("primaryButton"));

    buttons->addWidget(later_button);
    buttons->addWidget(download_button);
    layout->addLayout(buttons);

    connect(later_button, &QPushButton::clicked, this, &ModelMissingPanel::dismissed);
    connect(download_button, &QPushButton::clicked, this, &ModelMissingPanel::downloadRequested);
}
