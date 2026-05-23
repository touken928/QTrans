#include "app/ui/alert_panel.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

AlertPanel::AlertPanel(const QString & title, const QString & message, QWidget * parent)
    : QWidget(parent) {
    auto * layout = new QVBoxLayout(this);
    layout->setSpacing(12);

    auto * title_label = new QLabel(title, this);
    QFont title_font = title_label->font();
    title_font.setBold(true);
    title_font.setPointSize(title_font.pointSize() + 2);
    title_label->setFont(title_font);
    layout->addWidget(title_label);

    auto * body = new QLabel(message, this);
    body->setWordWrap(true);
    body->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(body);

    auto * buttons = new QHBoxLayout();
    buttons->addStretch(1);

    auto * ok_button = new QPushButton(QStringLiteral("OK"), this);
    ok_button->setObjectName(QStringLiteral("primaryButton"));
    buttons->addWidget(ok_button);
    layout->addLayout(buttons);

    connect(ok_button, &QPushButton::clicked, this, &AlertPanel::dismissed);
}
