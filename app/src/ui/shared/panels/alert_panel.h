#pragma once

#include <QWidget>

class AlertPanel : public QWidget {
    Q_OBJECT

public:
    explicit AlertPanel(const QString &title, const QString &message, QWidget *parent = nullptr);

signals:
    void dismissed();
};
