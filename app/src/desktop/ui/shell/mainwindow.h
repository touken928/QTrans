#pragma once

#include "domain/inference/inference_types.h"
#include "domain/download/download_types.h"
#include "domain/storage/app_paths.h"
#include "domain/settings/settings.h"
#include "ui/shell/page_id.h"

#include <QMainWindow>
#include <QString>

class BatchController;
class BatchPage;
class DownloadProgressPanel;
class DownloadService;
class HotkeyManager;
class InferenceService;
class LocalApiService;
class ModelUnavailableBanner;
class ModalOverlay;
class ModelPage;
class PopupWindow;
class PreferencesPage;
class SessionController;
class ShellStatusBar;
class SidebarWidget;
class SystemTray;
class TranslatePage;
class QStackedWidget;
class QThread;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(
        InferenceService *inference_service,
        DownloadService *download_service,
        BatchController *batch_controller,
        LocalApiService *local_api_service,
        QThread *worker_thread,
        const AppPaths &paths,
        QWidget *parent = nullptr);
    ~MainWindow() override;

    void bringToForeground();
    LocalApiService *localApiService() const;

protected:
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onPageSelected(PageId page);
    void onSaveModelSettings();
    void onLoadModelFromPage();
    void onUnloadModelFromPage();
    void onDeleteModel();
    void onDeleteModelForId(const QString &model_id);
    void onTranslateRequested(
        const QString &source,
        const QString &target_language,
        const QString &source_language,
        bool back_translate);
    void onCancelRequested();
    void onLanguageChanged();
    void onWordSelectSettingsChanged();
    void onTranslationStarted(TranslationJobId job_id);
    void onTranslationReset(TranslationJobId job_id, TranslationChannel channel);
    void onTranslationDelta(TranslationJobId job_id, TranslationChannel channel,
                            const QString &piece);
    void onTranslationFinished(const TranslationJobResult &result);
    void onStatusChanged(const QString &message, bool busy);
    void onModelLoadFinished(bool success, const QString &error_message, const QString &backend_label);
    void onModelUnloadFinished(bool success, const QString &error_message);
    void onDownloadStarted(DownloadId id);
    void onDownloadProgress(DownloadId id, qint64 downloaded, qint64 total,
                            double speed_bps, double eta_seconds);
    void onDownloadFinished(const DownloadResult &result);
    void onDownloadModelFromPage(const QString &model_id);

    // ── Batch slots ─────────────────────────────────────────────────────
    void onBatchAddFiles(const QStringList &paths, const QString &source_lang,
                         const QString &target_lang);
    void onBatchRemoveEntry(const QStringList &entry_ids);
    void onBatchRetry(const QStringList &entry_ids);
    void onBatchStart();
    void onBatchPause();
    void onBatchResume();
    void onBatchError(const QString &message);

private:
    void performStartupCheck();
    void initializeInferenceBackend();
    void syncSettingsToServices();
    void syncLanguagesToSettings();
    void saveSettings();
    void syncApiService();
    void setUiBusy(bool busy);
    void switchPage(PageId page);
    void refreshModelPage();
    void applySettingsFromPage();
    QString currentModelPath() const;
    bool isActiveTranslateJob(TranslationJobId job_id) const;

    // ── Shell state projection ──────────────────────────────────────────
    // The top bar and the unavailable-model banner are projections owned
    // by MainWindow: every label derives from signals this window already
    // receives, so pages never infer shell state themselves.
    QString configuredModelDisplayName() const;
    QString loadedModelDisplayName() const;
    void projectShellState();
    void refreshModelAvailability();

    void showAlertDialog(const QString &title, const QString &message);
    void showDownloadDialog();
    void hideModal();
    void startDownloadAndLoad();
    void startLoadModel();
    // Binds the synchronously reserved download id to its model id before
    // the triggering UI control returns, so the download counts as active
    // from reservation time and no second request can slip in.
    void bindActiveDownload(DownloadId id, const QString &model_id);

    InferenceService *inference_service_ = nullptr;
    DownloadService *download_service_ = nullptr;
    BatchController *batch_controller_ = nullptr;
    LocalApiService *local_api_service_ = nullptr;
    QThread *worker_thread_ = nullptr;
    AppPaths paths_;
    AppSettings settings_;

    QWidget *central_root_ = nullptr;
    QWidget *content_column_ = nullptr;
    ShellStatusBar *status_bar_ = nullptr;
    ModelUnavailableBanner *model_banner_ = nullptr;
    SidebarWidget *sidebar_ = nullptr;
    QStackedWidget *content_stack_ = nullptr;
    TranslatePage *translate_page_ = nullptr;
    ModelPage *model_page_ = nullptr;
    BatchPage *batch_page_ = nullptr;
    PreferencesPage *preferences_page_ = nullptr;
    ModalOverlay *modal_ = nullptr;
    DownloadProgressPanel *download_panel_ = nullptr;

    bool startup_checked_ = false;
    bool model_loaded_ = false;
    QString loaded_model_id_;
    bool busy_ = false;
    bool awaiting_download_load_ = false;
    bool own_translation_active_ = false;
    bool download_active_ = false;
    bool batch_running_ = false;
    // Paused is tracked separately from running: a paused batch keeps its
    // running flag in the controller, but the shell projects a distinct
    // status for it.
    bool batch_paused_ = false;
    bool load_failed_ = false;
    // Backend usage of the currently loaded model (from the truthful load
    // result); empty while no model is loaded. Projected by the bottom
    // status bar only.
    QString backend_label_;
    // True while the visible modal is the model-flow download panel, so a
    // load result only ever closes a modal this window opened for the
    // model lifecycle (never an unrelated alert/picker modal).
    bool model_flow_modal_active_ = false;
    // Banner dismissal is scoped to one configured model + availability
    // episode (file missing vs present); a different need still surfaces.
    QString dismissed_banner_model_id_;
    bool dismissed_banner_file_missing_ = false;
    QString current_status_message_;
    TranslationJobId active_translate_job_id_{};
    DownloadId active_download_id_{};
    // Lifecycle correlation: the model id whose file the accepted download
    // writes, the model id whose load is in flight, and whether an unload
    // is in progress. Only one operation is allowed at a time; stale event
    // ids are filtered before these ever change. The download id is bound
    // synchronously at reservation time (bindActiveDownload), so the active
    // state exists before any queued service event can race it.
    QString active_download_model_id_;
    QString loading_model_id_;
    bool unloading_ = false;
    qint64 last_download_done_ = 0;
    qint64 last_download_total_ = 0;

    SystemTray *system_tray_ = nullptr;
    HotkeyManager *hotkey_manager_ = nullptr;
    PopupWindow *popup_window_ = nullptr;
    SessionController *session_controller_ = nullptr;
};
