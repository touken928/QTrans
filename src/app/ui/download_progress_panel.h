#pragma once

#include <QWidget>

class QLabel;
class QProgressBar;
class QPushButton;

class DownloadProgressPanel : public QWidget {
    Q_OBJECT

public:
    explicit DownloadProgressPanel(QWidget *parent = nullptr);

    void setProgress(qint64 downloaded, qint64 total, double speed_bps, double eta_seconds);
    void setFailure();
    void setLoading();

signals:
    void cancelRequested();

private:
    QProgressBar *progress_bar_ = nullptr;
    QLabel *status_label_ = nullptr;
    QLabel *speed_label_ = nullptr;
    QLabel *eta_label_ = nullptr;
    QPushButton *cancel_button_ = nullptr;
};
