#pragma once

#include <QWidget>

class QLabel;
class QPushButton;

// Nonmodal workspace banner shown whenever the selected model is not
// available: no file downloaded yet (offers download) or present but not
// loaded (offers load). Navigation stays fully usable while it is shown;
// MainWindow decides visibility and state — this widget only renders.
class ModelUnavailableBanner : public QWidget {
    Q_OBJECT

public:
    explicit ModelUnavailableBanner(QWidget *parent = nullptr);

    void setState(bool file_missing, const QString &model_name);

signals:
    void downloadRequested();
    void loadRequested();
    void modelsRequested();
    void dismissed();

private:
    QLabel *title_label_ = nullptr;
    QLabel *detail_label_ = nullptr;
    QPushButton *primary_button_ = nullptr;
    bool file_missing_ = true;
};
