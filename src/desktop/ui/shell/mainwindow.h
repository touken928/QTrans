#pragma once

#include "domain/inference/inference_types.h"
#include "domain/download/download_types.h"
#include "domain/storage/app_paths.h"
#include "domain/settings/settings.h"

#include <QMainWindow>
#include <QString>

class BatchController;
class BatchLangPanel;
class BatchPage;
class DownloadProgressPanel;
class DownloadService;
class HotkeyManager;
class InferenceService;
class LocalApiService;
class ModalOverlay;
class ModelPage;
class PopupWindow;
class SessionController;
class SidebarWidget;
class SystemTray;
class TranslatePage;
class WordSelectPage;
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
    void onPageSelected(int index);
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
    void onModelUnloadFinished();
    void onDownloadStarted(DownloadId id);
    void onDownloadProgress(DownloadId id, qint64 downloaded, qint64 total,
                            double speed_bps, double eta_seconds);
    void onDownloadFinished(const DownloadResult &result);

    // ── Batch slots ─────────────────────────────────────────────────────
    void onBatchShowLanguagePicker();
    void onBatchAddFiles(const QString &source_lang, const QString &target_lang);
    void onBatchLanguagePickerCancelled();
    void onBatchRemoveEntry(const QStringList &entry_ids);
    void onBatchStart();
    void onBatchPause();
    void onBatchResume();
    void onBatchEntryAdded(const QString &entry_id,
                           const QString &source_language,
                           const QString &target_language);
    void onBatchEntryRemoved(const QString &entry_id);
    void onBatchEntryStateChanged(const QString &entry_id, int state);
    void onBatchSegmentProgress(const QString &entry_id, int completed, int total);
    void onBatchSaveEntry(const QStringList &entry_ids);
    void onBatchError(const QString &message);

private:
    void performStartupCheck();
    void initializeInferenceBackend();
    void syncSettingsToServices();
    void syncLanguagesToSettings();
    void saveSettings();
    void syncApiService();
    void setUiBusy(bool busy);
    void switchPage(int index);
    void refreshModelPage();
    void applySettingsFromPage();
    QString currentModelPath() const;
    bool isActiveTranslateJob(TranslationJobId job_id) const;

    void showModelMissingDialog();
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

    QWidget *central_root_ = nullptr;
    SidebarWidget *sidebar_ = nullptr;
    QStackedWidget *content_stack_ = nullptr;
    TranslatePage *translate_page_ = nullptr;
    ModelPage *model_page_ = nullptr;
    WordSelectPage *wordselect_page_ = nullptr;
    BatchPage *batch_page_ = nullptr;
    BatchLangPanel *batch_lang_panel_ = nullptr;
    ModalOverlay *modal_ = nullptr;
    DownloadProgressPanel *download_panel_ = nullptr;

    bool startup_checked_ = false;
    bool model_loaded_ = false;
    QString loaded_model_id_;
    bool busy_ = false;
    bool awaiting_download_load_ = false;
    bool own_translation_active_ = false;
    TranslationJobId active_translate_job_id_{};
    DownloadId active_download_id_{};

    SystemTray *system_tray_ = nullptr;
    HotkeyManager *hotkey_manager_ = nullptr;
    PopupWindow *popup_window_ = nullptr;
    SessionController *session_controller_ = nullptr;
};
