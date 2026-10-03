#pragma once

#include "download/download_types.h"

#include <QObject>
#include <QString>

class DownloadService;
class InferenceService;

// Owns the download -> load -> unload lifecycle correlation for the shell.
// It is a plain QObject (no widgets, no settings/paths access) that talks to
// DownloadService and InferenceService exactly the way MainWindow used to:
// direct method calls, with the services hopping threads themselves. MainWindow
// projects the state via accessors and reacts to the signals below; the widget
// updates live in MainWindow.
class ModelFlow : public QObject {
    Q_OBJECT

public:
    ModelFlow(InferenceService *inference_service, DownloadService *download_service,
              QObject *parent = nullptr);

    // Banner download: tells MainWindow to open the download dialog, reserves
    // the download synchronously, and binds it to model_id before returning.
    void beginDownloadThenLoad(const QString &model_id);
    // Page download: same reservation without a dialog. The caller has already
    // saved settings and synced them to the services.
    void beginPageDownloadThenLoad(const QString &model_id);
    // Starts a load for model_id. The caller has already synced settings.
    void beginLoad(const QString &model_id);
    // Starts an unload. The caller owns all gating (loaded/inference-active).
    void beginUnload();
    // Download panel cancel; clears the modal flag even for an invalid id.
    void cancelFromDialog();
    // Page cancel; a no-op unless a download is bound.
    void cancelFromPage();
    // Clears the model-flow modal flag only; MainWindow::hideModal calls this.
    void noteModalHidden();

    bool downloadActive() const {
        return download_active_;
    }
    DownloadId activeDownloadId() const {
        return active_download_id_;
    }
    QString activeDownloadModelId() const {
        return active_download_model_id_;
    }
    QString loadingModelId() const {
        return loading_model_id_;
    }
    bool unloading() const {
        return unloading_;
    }
    qint64 lastDownloadDone() const {
        return last_download_done_;
    }
    qint64 lastDownloadTotal() const {
        return last_download_total_;
    }

public slots:
    // Public so tests can inject a stale DownloadResult directly.
    void onDownloadStarted(DownloadId id);
    void onDownloadProgress(DownloadId id, qint64 downloaded, qint64 total,
                            double speed_bps, double eta_seconds);
    void onDownloadFinished(const DownloadResult &result);
    void onModelLoadFinished(bool success, const QString &error_message,
                             const QString &backend_label);
    void onModelUnloadFinished(bool success, const QString &error_message);

signals:
    void showDownloadDialog();
    void hideModalRequested();
    void alertRequested(const QString &title, const QString &message);
    void stateChanged();
    void downloadProgress(qint64 downloaded, qint64 total, double speed_bps,
                          double eta_seconds);
    void downloadFailed();
    void downloadReadyToLoad();
    void loadFinished(bool success, const QString &error_message,
                      const QString &backend_label);
    void unloadFinished(bool success, const QString &error_message);

private:
    // Binds a synchronously reserved download id to its model id before the
    // triggering UI control returns, so no second request can race the queued
    // downloadStarted event.
    void bindActiveDownload(DownloadId id, const QString &model_id);

    InferenceService *inference_service_ = nullptr;
    DownloadService *download_service_ = nullptr;

    bool awaiting_download_load_ = false;
    bool download_active_ = false;
    // Lifecycle correlation: the model id whose file the accepted download
    // writes, the model id whose load is in flight, and whether an unload is
    // in progress. Only one operation is allowed at a time; stale event ids are
    // filtered before these ever change.
    DownloadId active_download_id_{};
    QString active_download_model_id_;
    QString loading_model_id_;
    bool unloading_ = false;
    // True while the visible modal is the model-flow download panel, so a load
    // result only ever closes a modal opened for the model lifecycle.
    bool model_flow_modal_active_ = false;
    qint64 last_download_done_ = 0;
    qint64 last_download_total_ = 0;
};
