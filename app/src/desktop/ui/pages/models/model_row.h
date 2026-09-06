#pragma once

#include <QWidget>

class QLabel;
class QProgressBar;
class QPushButton;

// One dense row in the operational model library. Rows are created once per
// catalog entry and updated in place — the page never rebuilds them on
// refresh, so the configured highlight, scroll position and focus survive
// every state push. The row renders truthfully from the state MainWindow
// pushes: status (loaded / ready / not downloaded / unavailable), lifecycle
// activity (loading / unloading / downloading with progress), and the
// configured marker. Buttons are enabled only when their action is safe, and
// competing actions are disabled while any lifecycle operation runs.
class ModelRow : public QWidget {
    Q_OBJECT

public:
    enum class Status {
        Loaded,         // file loaded into the runtime
        Ready,          // file present, not loaded
        NotDownloaded,  // no file yet
        Unavailable,    // backend cannot run this model (detail text)
    };

    enum class Activity {
        Idle,
        Loading,      // load in progress on this row
        Unloading,    // unload in progress (gates the whole library)
        Downloading,  // download in progress on this row
    };

    explicit ModelRow(const QString &model_id, const QString &display_name,
                      const QString &filename, QWidget *parent = nullptr);

    QString modelId() const {
        return model_id_;
    }

    void setStatus(Status status, const QString &detail = {});
    void setActivity(Activity activity);
    void setConfigured(bool configured);
    void setFilePresent(bool present);
    // Gates every action while any lifecycle operation runs anywhere in the
    // library (only one operation is allowed at a time).
    void setLibraryBusy(bool busy);
    // Gates lifecycle actions while interactive or batch inference runs, so
    // a load/unload/download can never conflict with live work.
    void setInferenceBusy(bool busy);
    void setDownloadProgress(qint64 downloaded, qint64 total);

signals:
    void loadClicked(const QString &model_id);
    void downloadClicked(const QString &model_id);
    void unloadClicked(const QString &model_id);
    void deleteClicked(const QString &model_id);
    void cancelDownloadClicked(const QString &model_id);

private:
    void updatePresentation();

    QString model_id_;
    QString display_name_;

    QLabel *name_label_ = nullptr;
    QLabel *file_label_ = nullptr;
    QLabel *status_dot_ = nullptr;
    QLabel *status_label_ = nullptr;
    QLabel *configured_badge_ = nullptr;
    QProgressBar *progress_bar_ = nullptr;
    QPushButton *load_btn_ = nullptr;
    QPushButton *download_btn_ = nullptr;
    QPushButton *unload_btn_ = nullptr;
    QPushButton *delete_btn_ = nullptr;
    QPushButton *cancel_btn_ = nullptr;

    Status status_ = Status::NotDownloaded;
    Activity activity_ = Activity::Idle;
    bool configured_ = false;
    bool file_present_ = false;
    bool library_busy_ = false;
    bool inference_busy_ = false;
    qint64 download_done_ = 0;
    qint64 download_total_ = 0;
};
