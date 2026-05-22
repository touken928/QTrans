#pragma once

#include <QWidget>

class ModelMissingPanel : public QWidget {
    Q_OBJECT

public:
    explicit ModelMissingPanel(const QString & mode_label, const QString & model_path, QWidget * parent = nullptr);

signals:
    void dismissed();
    void downloadRequested();
};
