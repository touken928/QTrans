#pragma once

#include "translate/inference_types.h"
#include "download/runtime_capabilities.h"
#include "download/download_types.h"
#include "paths/app_paths.h"
#include "settings/settings.h"
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
class ModelFlow;
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

    InferenceService *inference_service_ = nullptr;
    DownloadService *download_service_ = nullptr;
    BatchController *batch_controller_ = nullptr;
    LocalApiService *local_api_service_ = nullptr;
    QThread *worker_thread_ = nullptr;
    AppPaths paths_;
    AppSettings settings_;
    RuntimeCapabilities runtime_caps_;

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
    bool own_translation_active_ = false;
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
    // Banner dismissal is scoped to one configured model + availability
    // episode (file missing vs present); a different need still surfaces.
    QString dismissed_banner_model_id_;
    bool dismissed_banner_file_missing_ = false;
    QString current_status_message_;
    TranslationJobId active_translate_job_id_{};

    // Owns the download -> load -> unload correlation fields and talks to the
    // services directly; this window only projects its state.
    ModelFlow *model_flow_ = nullptr;

    SystemTray *system_tray_ = nullptr;
    HotkeyManager *hotkey_manager_ = nullptr;
    PopupWindow *popup_window_ = nullptr;
    SessionController *session_controller_ = nullptr;
};
