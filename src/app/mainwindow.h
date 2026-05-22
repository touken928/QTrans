#pragma once

#include "core/app_paths.h"
#include "settings/settings.h"

#include <QMainWindow>

class Backend;
class DownloadProgressPanel;
class ModalOverlay;
class ModelPage;
class SidebarWidget;
class TranslatePage;
class QStackedWidget;
class QThread;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(Backend * backend, QThread * worker_thread, QWidget * parent = nullptr);
    ~MainWindow() override;

protected:
    void showEvent(QShowEvent * event) override;

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
    void onTranslationFinished();
    void onStatusChanged(const QString & message, bool busy);
    void onModelLoadFinished(bool success);
    void onModelUnloadFinished();
    void onDownloadProgress(qint64 downloaded, qint64 total, double speed_bps, double eta_seconds);
    void onDownloadFinished(bool success);
    void onTargetReset();
    void onTargetAppended(const QString & piece);
    void onBackTranslateReset();
    void onBackTranslateAppended(const QString & piece);

private:
    void performStartupCheck();
    void syncSettingsToBackend();
    void saveSettings();
    void setUiBusy(bool busy);
    void switchPage(int index);
    void refreshModelPage();
    void applySettingsFromPage();
    QString currentModelPath() const;

    void showModelMissingDialog();
    void showDownloadDialog();
    void hideModal();
    void startDownloadAndLoad();
    void startLoadModel();

    Backend * backend_ = nullptr;
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
    bool pending_back_translate_ = false;
    QString pending_back_language_;
    QString pending_forward_translation_;
};
