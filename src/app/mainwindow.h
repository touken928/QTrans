#pragma once

#include "storage/app_paths.h"
#include "storage/settings.h"

#include <QMainWindow>

class DownloadProgressPanel;
class HotkeyManager;
class ModalOverlay;
class ModelPage;
class PopupWindow;
class SessionController;
class SidebarWidget;
class SystemTray;
class TaskService;
class TranslatePage;
class QStackedWidget;
class QThread;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(TaskService * task_service, QThread * worker_thread, QWidget * parent = nullptr);
    ~MainWindow() override;

protected:
    void showEvent(QShowEvent * event) override;
    void closeEvent(QCloseEvent * event) override;

private slots:
    void onPageSelected(int index);
    void onSaveModelSettings();
    void onLoadModelFromPage();
    void onUnloadModelFromPage();
    void onTranslateRequested(
        const QString & source,
        const QString & target_language,
        const QString & source_language,
        bool back_translate);
    void onCancelRequested();
    void onLanguageChanged();
    void onTranslateTaskStarted(quint64 task_id);
    void onTranslationFinished(quint64 task_id, int state);
    void onStatusChanged(const QString & message, bool busy);
    void onModelLoadFinished(bool success, const QString & error_message);
    void onModelUnloadFinished();
    void onDownloadProgress(qint64 downloaded, qint64 total, double speed_bps, double eta_seconds);
    void onDownloadFinished(bool success);
    void onTargetReset(quint64 task_id);
    void onTargetAppended(quint64 task_id, const QString & piece);
    void onBackTranslateReset(quint64 task_id);
    void onBackTranslateAppended(quint64 task_id, const QString & piece);

private:
    void performStartupCheck();
    void syncSettingsToTaskService();
    void syncLanguagesToSettings();
    void saveSettings();
    void setUiBusy(bool busy);
    void switchPage(int index);
    void refreshModelPage();
    void applySettingsFromPage();
    QString currentModelPath() const;
    bool isActiveTranslateTask(quint64 task_id) const;

    void showModelMissingDialog();
    void showAlertDialog(const QString & title, const QString & message);
    void showDownloadDialog();
    void hideModal();
    void startDownloadAndLoad();
    void startLoadModel();

    TaskService * task_service_ = nullptr;
    QThread * worker_thread_ = nullptr;
    AppPaths paths_;
    AppSettings settings_;

    QWidget * central_root_ = nullptr;
    SidebarWidget * sidebar_ = nullptr;
    QStackedWidget * content_stack_ = nullptr;
    TranslatePage * translate_page_ = nullptr;
    ModelPage * model_page_ = nullptr;
    ModalOverlay * modal_ = nullptr;
    DownloadProgressPanel * download_panel_ = nullptr;

    bool startup_checked_ = false;
    bool model_loaded_ = false;
    bool busy_ = false;
    bool awaiting_download_load_ = false;
    bool own_translation_active_ = false;
    quint64 active_translate_task_id_ = 0;

    SystemTray * system_tray_ = nullptr;
    HotkeyManager * hotkey_manager_ = nullptr;
    PopupWindow * popup_window_ = nullptr;
    SessionController * session_controller_ = nullptr;
};
