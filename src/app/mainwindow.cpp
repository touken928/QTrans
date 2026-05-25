#include "app/mainwindow.h"

#include "app/modal_overlay.h"
#include "app/task_service.h"
#include "app/ui/app_theme.h"
#include "app/ui/alert_panel.h"
#include "app/ui/download_progress_panel.h"
#include "app/ui/model_missing_panel.h"
#include "app/ui/model_page.h"
#include "app/ui/sidebar_widget.h"
#include "app/ui/translate_page.h"
#include "network/download.h"
#include "model/model_catalog.h"
#include "wordselect/hotkey_manager.h"
#include "wordselect/popup_window.h"
#include "wordselect/session_controller.h"
#include "wordselect/system_tray.h"

#include <QCloseEvent>
#include <QCoreApplication>
#include <QDebug>
#include <QHBoxLayout>
#include <QMetaObject>
#include <QShowEvent>
#include <QStackedWidget>
#include <QThread>

MainWindow::MainWindow(TaskService * task_service, QThread * worker_thread, QWidget * parent)
    : QMainWindow(parent)
    , task_service_(task_service)
    , worker_thread_(worker_thread)
    , paths_(AppPaths::detect(QCoreApplication::applicationDirPath().toStdString())) {
    setWindowTitle(QStringLiteral("QTrans"));
    resize(960, 600);
    setMinimumSize(720, 480);

    paths_.ensureDirectories();
    settings_.load(paths_);
    settings_.ensureStorage(paths_);
    syncSettingsToTaskService();

    central_root_ = new QWidget(this);
    central_root_->setObjectName(QStringLiteral("centralRoot"));
    auto * shell = new QHBoxLayout(central_root_);
    shell->setContentsMargins(0, 0, 0, 0);
    shell->setSpacing(0);

    sidebar_ = new SidebarWidget(central_root_);
    shell->addWidget(sidebar_);

    translate_page_ = new TranslatePage(central_root_);
    model_page_ = new ModelPage(central_root_);
    model_page_->setSettings(paths_, settings_);

    content_stack_ = new QStackedWidget(central_root_);
    content_stack_->addWidget(translate_page_);
    content_stack_->addWidget(model_page_);
    shell->addWidget(content_stack_, 1);

    setCentralWidget(central_root_);
    AppTheme::apply(this);

    modal_ = new ModalOverlay(central_root_);
    switchPage(0);

    connect(sidebar_, &SidebarWidget::pageSelected, this, &MainWindow::onPageSelected);
    connect(model_page_, &ModelPage::saveRequested, this, &MainWindow::onSaveModelSettings);
    connect(model_page_, &ModelPage::loadModelRequested, this, &MainWindow::onLoadModelFromPage);
    connect(model_page_, &ModelPage::unloadModelRequested, this, &MainWindow::onUnloadModelFromPage);
    connect(model_page_, &ModelPage::modelEdited, this, &MainWindow::applySettingsFromPage);
    connect(translate_page_, &TranslatePage::translateRequested, this, &MainWindow::onTranslateRequested);
    connect(translate_page_, &TranslatePage::cancelRequested, this, &MainWindow::onCancelRequested);
    connect(translate_page_, &TranslatePage::languageChanged, this, &MainWindow::onLanguageChanged);

    connect(task_service_, &TaskService::statusChanged, this, &MainWindow::onStatusChanged);
    connect(task_service_, &TaskService::modelLoadFinished, this, &MainWindow::onModelLoadFinished);
    connect(task_service_, &TaskService::modelUnloadFinished, this, &MainWindow::onModelUnloadFinished);
    connect(task_service_, &TaskService::downloadProgress, this, &MainWindow::onDownloadProgress);
    connect(task_service_, &TaskService::downloadFinished, this, &MainWindow::onDownloadFinished);
    connect(task_service_, &TaskService::targetReset, this, &MainWindow::onTargetReset);
    connect(task_service_, &TaskService::targetAppended, this, &MainWindow::onTargetAppended);
    connect(task_service_, &TaskService::backTranslateReset, this, &MainWindow::onBackTranslateReset);
    connect(task_service_, &TaskService::backTranslateAppended, this, &MainWindow::onBackTranslateAppended);
    connect(task_service_, &TaskService::translationFinished, this, &MainWindow::onTranslationFinished);
    connect(task_service_, &TaskService::translateTaskStarted, this, &MainWindow::onTranslateTaskStarted);

    hotkey_manager_ = new HotkeyManager(this);

    popup_window_ = new PopupWindow(nullptr);

    session_controller_ = new SessionController(
        hotkey_manager_, task_service_, popup_window_, this);
    session_controller_->initialize();
    const QString hotkeyStr = QString::fromStdString(settings_.hotkey);
    if (!hotkeyStr.isEmpty()) {
        session_controller_->setHotkey(hotkeyStr);
    }

    translate_page_->setSourceLanguage(QString::fromStdString(settings_.source_language));
    translate_page_->setTargetLanguage(QString::fromStdString(settings_.target_language));
    syncLanguagesToSettings();

    system_tray_ = new SystemTray(this);
    connect(system_tray_, &SystemTray::openMainWindow, this, [this]() {
        show();
        raise();
        activateWindow();
    });
    connect(system_tray_, &SystemTray::toggleTranslation,
            session_controller_, &SessionController::setEnabled);
    connect(system_tray_, &SystemTray::quitApp, qApp, &QCoreApplication::quit);
}

MainWindow::~MainWindow() {
    if (worker_thread_ != nullptr) {
        worker_thread_->quit();
        worker_thread_->wait();
    }
}

void MainWindow::closeEvent(QCloseEvent * event) {
    Q_UNUSED(event);
    hide();
    event->ignore();
}

void MainWindow::showEvent(QShowEvent * event) {
    QMainWindow::showEvent(event);

    if (!startup_checked_) {
        startup_checked_ = true;
        modal_->setGeometry(central_root_->rect());
        performStartupCheck();
    }
}

void MainWindow::onPageSelected(int index) {
    switchPage(index);
}

void MainWindow::switchPage(int index) {
    content_stack_->setCurrentIndex(index);
    sidebar_->setCurrentPage(index);

    if (index == 1) {
        refreshModelPage();
    }
}

void MainWindow::refreshModelPage() {
    model_page_->setSettings(paths_, settings_);
    model_page_->setModelLoaded(model_loaded_);
}

void MainWindow::applySettingsFromPage() {
    model_page_->applyTo(settings_);
    syncSettingsToTaskService();
}

QString MainWindow::currentModelPath() const {
    return QString::fromStdString(settings_.effectiveModelPath(paths_));
}

void MainWindow::syncSettingsToTaskService() {
    const ModelCatalogEntry * model = settings_.selectedModel();
    task_service_->setModelPath(currentModelPath());
    task_service_->setRemoteSpec(QString::fromStdString(model->remote_spec));
    task_service_->setDownloadHub(model->download_hub);
}

void MainWindow::syncLanguagesToSettings() {
    const QString source = translate_page_->sourceLanguageName();
    const QString target = translate_page_->targetLanguageName();
    settings_.source_language = source.toStdString();
    settings_.target_language = target.toStdString();
    saveSettings();
    session_controller_->setTranslateLanguages(source, target);
}

void MainWindow::saveSettings() {
    try {
        settings_.save(paths_);
    } catch (const std::exception & ex) {
        translate_page_->setStatus(QString::fromUtf8(ex.what()));
    }
}

void MainWindow::onSaveModelSettings() {
    applySettingsFromPage();
    settings_.ensureStorage(paths_);
    saveSettings();
    syncSettingsToTaskService();
    refreshModelPage();
    translate_page_->setStatus(QStringLiteral("Model settings saved"));
}

void MainWindow::onLoadModelFromPage() {
    applySettingsFromPage();
    settings_.ensureStorage(paths_);
    saveSettings();
    syncSettingsToTaskService();
    refreshModelPage();

    if (download_file_exists(settings_.effectiveModelPath(paths_))) {
        startLoadModel();
        return;
    }

    showModelMissingDialog();
}

void MainWindow::onUnloadModelFromPage() {
    if (!model_loaded_) {
        return;
    }

    QMetaObject::invokeMethod(task_service_, "unloadModel", Qt::QueuedConnection);
}

void MainWindow::setUiBusy(bool busy) {
    busy_ = busy;
    sidebar_->setNavigationEnabled(!busy);
    translate_page_->setBusy(busy);
    translate_page_->setModelLoaded(model_loaded_);
    model_page_->setBusy(busy);
    model_page_->setModelLoaded(model_loaded_);
}

void MainWindow::performStartupCheck() {
    if (download_file_exists(settings_.effectiveModelPath(paths_))) {
        startLoadModel();
        return;
    }

    showModelMissingDialog();
}

void MainWindow::hideModal() {
    modal_->hideModal();
}

void MainWindow::showModelMissingDialog() {
    auto * panel = new ModelMissingPanel(
        QString::fromStdString(paths_.modeLabel()),
        currentModelPath());
    connect(panel, &ModelMissingPanel::dismissed, this, [this]() {
        hideModal();
        translate_page_->setStatus(QStringLiteral("Model not loaded. Open Model to download or load."));
    });
    connect(panel, &ModelMissingPanel::downloadRequested, this, &MainWindow::startDownloadAndLoad);

    modal_->setContent(panel, QSize(500, 260));
    modal_->showModal();
}

void MainWindow::showDownloadDialog() {
    download_panel_ = new DownloadProgressPanel();
    modal_->setContent(download_panel_, QSize(460, 220));
    modal_->showModal();
}

void MainWindow::showAlertDialog(const QString & title, const QString & message) {
    auto * panel = new AlertPanel(title, message);
    connect(panel, &AlertPanel::dismissed, this, &MainWindow::hideModal);

    modal_->setContent(panel, QSize(500, 220));
    modal_->showModal();
}

void MainWindow::startDownloadAndLoad() {
    awaiting_download_load_ = true;
    showDownloadDialog();
    QMetaObject::invokeMethod(task_service_, "downloadModel", Qt::QueuedConnection);
}

void MainWindow::startLoadModel() {
    syncSettingsToTaskService();
    QMetaObject::invokeMethod(task_service_, "loadModel", Qt::QueuedConnection);
}

void MainWindow::onTranslateRequested(
    const QString & source,
    const QString & target_language,
    const QString & source_language,
    bool back_translate) {
    active_translate_task_id_ = 0;
    own_translation_active_ = true;

    QMetaObject::invokeMethod(
        task_service_,
        "translateInteractive",
        Qt::QueuedConnection,
        Q_ARG(QString, source),
        Q_ARG(QString, target_language),
        Q_ARG(QString, source_language),
        Q_ARG(bool, back_translate));

    translate_page_->setTranslating(true);
}

void MainWindow::onTranslateTaskStarted(quint64 task_id) {
    qDebug() << "[MainWindow] translateTaskStarted task:" << task_id << "own_active:" << own_translation_active_ << "thread:" << QThread::currentThread();
    if (own_translation_active_) {
        active_translate_task_id_ = task_id;
    }
}

void MainWindow::onCancelRequested() {
    qDebug() << "[MainWindow] cancelRequested task:" << active_translate_task_id_ << "thread:" << QThread::currentThread();
    QMetaObject::invokeMethod(
        task_service_,
        "cancelTask",
        Qt::DirectConnection,
        Q_ARG(quint64, active_translate_task_id_));
}

void MainWindow::onLanguageChanged() {
    syncLanguagesToSettings();
}

bool MainWindow::isActiveTranslateTask(quint64 task_id) const {
    return active_translate_task_id_ != 0 && active_translate_task_id_ == task_id;
}

void MainWindow::onTranslationFinished(quint64 task_id, int state) {
    Q_UNUSED(state);
    qDebug() << "[MainWindow] translationFinished task:" << task_id << "active:" << active_translate_task_id_ << "own:" << own_translation_active_ << "thread:" << QThread::currentThread();
    if (!isActiveTranslateTask(task_id)) {
        return;
    }

    active_translate_task_id_ = 0;
    own_translation_active_ = false;
    translate_page_->setTranslating(false);
}

void MainWindow::onStatusChanged(const QString & message, bool busy) {
    translate_page_->setStatus(busy ? message + QStringLiteral(" ...") : message);
    setUiBusy(busy);
}

void MainWindow::onTargetReset(quint64 task_id) {
    if (!isActiveTranslateTask(task_id)) {
        return;
    }
    qDebug() << "[MainWindow] targetReset task:" << task_id << "thread:" << QThread::currentThread();
    translate_page_->resetTarget();
}

void MainWindow::onTargetAppended(quint64 task_id, const QString & piece) {
    if (!isActiveTranslateTask(task_id)) {
        return;
    }
    qDebug() << "[MainWindow] targetAppended task:" << task_id << "len:" << piece.size() << "thread:" << QThread::currentThread();
    translate_page_->appendTarget(piece);
}

void MainWindow::onModelLoadFinished(bool success, const QString & error_message) {
    model_loaded_ = success;
    translate_page_->setModelLoaded(success);
    model_page_->setModelLoaded(success);
    setUiBusy(busy_);

    if (success) {
        if (modal_->isVisible()) {
            hideModal();
            switchPage(0);
        }
        return;
    }

    QString message = error_message.trimmed();
    if (message.isEmpty()) {
        message = QStringLiteral("Failed to load the model.");
    }
    showAlertDialog(QStringLiteral("Failed to Load Model"), message);
}

void MainWindow::onModelUnloadFinished() {
    model_loaded_ = false;
    translate_page_->setModelLoaded(false);
    model_page_->setModelLoaded(false);
    setUiBusy(busy_);
}

void MainWindow::onDownloadProgress(
    qint64 downloaded,
    qint64 total,
    double speed_bps,
    double eta_seconds) {
    if (download_panel_ != nullptr) {
        download_panel_->setProgress(downloaded, total, speed_bps, eta_seconds);
    }
}

void MainWindow::onDownloadFinished(bool success) {
    if (!success) {
        awaiting_download_load_ = false;
        if (download_panel_ != nullptr) {
            download_panel_->setFailure();
        }
        return;
    }

    if (awaiting_download_load_) {
        awaiting_download_load_ = false;
        if (download_panel_ != nullptr) {
            download_panel_->setLoading();
        }
        startLoadModel();
    }
}

void MainWindow::onBackTranslateReset(quint64 task_id) {
    if (!isActiveTranslateTask(task_id)) {
        return;
    }
    translate_page_->resetBackTranslate();
}

void MainWindow::onBackTranslateAppended(quint64 task_id, const QString & piece) {
    if (!isActiveTranslateTask(task_id)) {
        return;
    }
    translate_page_->appendBackTranslate(piece);
}
