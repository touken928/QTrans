#include "ui/shell/mainwindow.h"

#include "ui/shared/modal_overlay.h"
#include "app/batch_controller.h"
#include "app/task_service.h"
#include "ui/pages/batch/batch_lang_panel.h"
#include "ui/shared/theme/app_theme.h"
#include "ui/shared/panels/alert_panel.h"
#include "ui/shared/panels/download_progress_panel.h"
#include "ui/shared/panels/model_missing_panel.h"
#include "ui/pages/batch/batch_page.h"
#include "ui/pages/models/model_page.h"
#include "ui/sidebar/sidebar_widget.h"
#include "ui/pages/translate/translate_page.h"
#include "ui/pages/wordselect/wordselect_page.h"
#include "domain/batch/batch_enums.h"
#include "domain/download/download.h"
#include "domain/model-catalog/model_catalog.h"
#include "domain/inference/runtime_capabilities.h"
#include "shared/string_bridge.h"
#include "domain/logging/component.h"
#include "domain/logging/logger.h"
#include "domain/platform/hotkeys/hotkey_manager.h"
#include "ui/popup/popup_window.h"
#include "ui/popup/session_controller.h"
#include "ui/popup/system_tray.h"

#include <QCloseEvent>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QMetaObject>
#include <QPushButton>
#include <QShowEvent>
#include <QStackedWidget>
#include <QThread>
#include <QUrl>
#include <QVariantMap>

MainWindow::MainWindow(
    TaskService *task_service,
    BatchController *batch_controller,
    QThread *worker_thread,
    const AppPaths &paths,
    QWidget *parent)
    : QMainWindow(parent), task_service_(task_service), batch_controller_(batch_controller), worker_thread_(worker_thread), paths_(paths) {
    setWindowTitle(QStringLiteral("QTrans"));
    resize(960, 600);
    setMinimumSize(720, 480);

    settings_.load(paths_);
    settings_.ensureStorage(paths_);
    initializeInferenceBackend();
    settings_.migrateModelSelection(RuntimeCapabilities::instance());
    syncSettingsToTaskService();

    central_root_ = new QWidget(this);
    central_root_->setObjectName(QStringLiteral("centralRoot"));
    auto *shell = new QHBoxLayout(central_root_);
    shell->setContentsMargins(0, 0, 0, 0);
    shell->setSpacing(0);

    sidebar_ = new SidebarWidget(central_root_);
    shell->addWidget(sidebar_);

    translate_page_ = new TranslatePage(central_root_);
    model_page_ = new ModelPage(central_root_);
    model_page_->setSettings(paths_, settings_);
    wordselect_page_ = new WordSelectPage(central_root_);
    batch_page_ = new BatchPage(central_root_);

    content_stack_ = new QStackedWidget(central_root_);
    content_stack_->addWidget(translate_page_);   // index 0
    content_stack_->addWidget(wordselect_page_);  // index 1
    content_stack_->addWidget(batch_page_);       // index 2
    content_stack_->addWidget(model_page_);       // index 3
    shell->addWidget(content_stack_, 1);

    setCentralWidget(central_root_);
    AppTheme::apply(this);

    modal_ = new ModalOverlay(central_root_);
    switchPage(0);

    connect(sidebar_, &SidebarWidget::pageSelected, this, &MainWindow::onPageSelected);
    connect(model_page_, &ModelPage::loadModelRequested, this, [this](const QString &model_id) {
        settings_.setSelectedModelId(qtrans::app::to_utf8(model_id));
        applySettingsFromPage();
        onLoadModelFromPage();
    });
    connect(model_page_, &ModelPage::unloadModelRequested, this, [this](const QString &model_id) {
        if (model_id == loaded_model_id_ || model_id.isEmpty()) {
            onUnloadModelFromPage();
        }
    });
    connect(model_page_, &ModelPage::deleteModelRequested, this, [this](const QString &model_id) {
        onDeleteModelForId(model_id);
    });
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

    // ── Batch page wiring (batch controller lives on worker thread;
    //     use queued invocations for UI→worker calls) ─────────────────────
    connect(batch_page_, &BatchPage::addFilesRequested,
            this, &MainWindow::onBatchShowLanguagePicker);
    connect(batch_page_, &BatchPage::removeSelectedRequested,
            this, &MainWindow::onBatchRemoveEntry);
    connect(batch_page_, &BatchPage::startRequested,
            this, &MainWindow::onBatchStart);
    connect(batch_page_, &BatchPage::pauseRequested,
            this, &MainWindow::onBatchPause);
    connect(batch_page_, &BatchPage::resumeRequested,
            this, &MainWindow::onBatchResume);
    connect(batch_page_, &BatchPage::saveRequested,
            this, &MainWindow::onBatchSaveEntry);

    // BatchController signals (emitted from worker thread) → page updates
    // on UI thread. Auto-connection becomes queued cross-thread.
    connect(batch_controller_, &BatchController::entryAdded,
            this, &MainWindow::onBatchEntryAdded);
    connect(batch_controller_, &BatchController::entryRemoved,
            this, &MainWindow::onBatchEntryRemoved);
    connect(batch_controller_, &BatchController::entrySaved,
            this, [this](const QString &entry_id, const QString &path) {
                batch_page_->setCardSaved(entry_id, path);
            });
    connect(batch_controller_, &BatchController::entryStateChanged,
            this, &MainWindow::onBatchEntryStateChanged);
    connect(batch_controller_, &BatchController::segmentProgress,
            this, &MainWindow::onBatchSegmentProgress);
    connect(batch_controller_, &BatchController::batchStateChanged,
            this, [this](bool running, bool paused) {
                batch_page_->setRunning(running);
                batch_page_->setPaused(paused);
            });
    connect(batch_controller_, &BatchController::batchFinished,
            this, [this]() {
                batch_page_->setRunning(false);
                batch_page_->setStatusText(QStringLiteral("Batch finished"));
            });
    connect(batch_controller_, &BatchController::errorOccurred,
            this, &MainWindow::onBatchError);

    // Load persisted queue entries so the UI auto-syncs.
    QMetaObject::invokeMethod(batch_controller_, "loadPersistedEntries",
                              Qt::QueuedConnection);

    hotkey_manager_ = new HotkeyManager(this);

    popup_window_ = new PopupWindow(nullptr);

    session_controller_ = new SessionController(
        hotkey_manager_, task_service_, popup_window_, this);
    session_controller_->initialize();
    const QString hotkeyStr = qtrans::app::from_utf8(settings_.hotkey);
    if (!hotkeyStr.isEmpty()) {
        session_controller_->setHotkey(hotkeyStr);
    }
    session_controller_->setTranslateLanguages(
        qtrans::app::from_utf8(settings_.wordselect_source_language),
        QStringLiteral("Auto"));
    session_controller_->setEnabled(settings_.wordselect_enabled);

    translate_page_->setSourceLanguage(qtrans::app::from_utf8(settings_.source_language));
    translate_page_->setTargetLanguage(qtrans::app::from_utf8(settings_.target_language));
    syncLanguagesToSettings();

    wordselect_page_->setEnabled(settings_.wordselect_enabled);
    wordselect_page_->setCloseToTray(settings_.close_to_tray);
    wordselect_page_->setTargetLanguage(qtrans::app::from_utf8(settings_.wordselect_target_language));
    wordselect_page_->setHotkey(hotkeyStr);
    wordselect_page_->setAutoCloseMs(settings_.auto_close_ms);
    connect(wordselect_page_, &WordSelectPage::settingsChanged,
            this, &MainWindow::onWordSelectSettingsChanged);

    system_tray_ = new SystemTray(this);
    connect(system_tray_, &SystemTray::openMainWindow, this, &MainWindow::bringToForeground);
    connect(system_tray_, &SystemTray::toggleTranslation,
            session_controller_, &SessionController::setEnabled);
    connect(system_tray_, &SystemTray::quitApp, qApp, &QCoreApplication::quit);
}

MainWindow::~MainWindow() = default;

void MainWindow::bringToForeground() {
    show();
    raise();
    activateWindow();
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (settings_.close_to_tray) {
        hide();
        event->ignore();
    } else {
        event->accept();
        QCoreApplication::quit();
    }
}

void MainWindow::showEvent(QShowEvent *event) {
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

    if (index == 3) {
        refreshModelPage();
    }
}

void MainWindow::refreshModelPage() {
    model_page_->setRuntimeCapabilities(RuntimeCapabilities::instance());
    model_page_->setSettings(paths_, settings_);
    model_page_->setModelLoaded(model_loaded_);
    model_page_->setLoadedModelId(loaded_model_id_);
}

void MainWindow::initializeInferenceBackend() {
    QMetaObject::invokeMethod(task_service_, "initializeBackend", Qt::BlockingQueuedConnection);
}

void MainWindow::applySettingsFromPage() {
    model_page_->applyTo(settings_);
    syncSettingsToTaskService();
}

QString MainWindow::currentModelPath() const {
    return qtrans::app::from_utf8(settings_.effectiveModelPath(paths_));
}

void MainWindow::syncSettingsToTaskService() {
    const ModelCatalogEntry *model = settings_.selectedModel();
    task_service_->setModelId(qtrans::app::from_utf8(model->id));
    task_service_->setModelPath(currentModelPath());
    task_service_->setRemoteSpec(qtrans::app::from_utf8(model->remote_spec));
    task_service_->setModelscopeRemoteSpec(qtrans::app::from_utf8(model->modelscope_remote_spec));
    task_service_->setDownloadHub(model->download_hub);
}

void MainWindow::syncLanguagesToSettings() {
    const QString source = translate_page_->sourceLanguageName();
    const QString target = translate_page_->targetLanguageName();
    settings_.source_language = qtrans::app::to_utf8(source);
    settings_.target_language = qtrans::app::to_utf8(target);
    saveSettings();
}

void MainWindow::onWordSelectSettingsChanged() {
    const bool enabled = wordselect_page_->isEnabled();
    const bool close_to_tray = wordselect_page_->isCloseToTray();
    const QString target = wordselect_page_->targetLanguage();
    const QString hotkey = wordselect_page_->hotkey();
    const int auto_close = wordselect_page_->autoCloseMs();

    settings_.wordselect_enabled = enabled;
    settings_.close_to_tray = close_to_tray;
    settings_.wordselect_target_language = qtrans::app::to_utf8(target);
    settings_.hotkey = qtrans::app::to_utf8(hotkey);
    settings_.auto_close_ms = auto_close;
    saveSettings();

    session_controller_->setEnabled(enabled);
    session_controller_->setTranslateLanguages(QStringLiteral("Auto"), target);
    if (!hotkey.isEmpty()) {
        session_controller_->setHotkey(hotkey);
    }
}

void MainWindow::saveSettings() {
    try {
        settings_.save(paths_);
    } catch (const std::exception &ex) {
        translate_page_->setStatus(qtrans::app::from_utf8(ex.what()));
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

void MainWindow::onDeleteModel() {
    onDeleteModelForId(qtrans::app::from_utf8(settings_.model_id));
}

void MainWindow::onDeleteModelForId(const QString &model_id) {
    const auto *entry = find_model_by_id(qtrans::app::to_utf8(model_id));
    if (entry == nullptr) {
        return;
    }
    const QString model_path = qtrans::app::from_utf8(
        (std::filesystem::path(settings_.effectiveModelsDir(paths_)) / entry->filename).string());

    auto *msg = new QMessageBox(this);
    msg->setObjectName(QStringLiteral("alertPanel"));
    msg->setIcon(QMessageBox::Warning);
    msg->setWindowTitle(QStringLiteral("Delete Model File"));
    msg->setText(QStringLiteral("Delete the model file?\n%1\n\nThis cannot be undone.").arg(model_path));
    auto *delete_btn = msg->addButton(QStringLiteral("Delete"), QMessageBox::DestructiveRole);
    msg->addButton(QMessageBox::Cancel);
    msg->setDefaultButton(QMessageBox::Cancel);
    msg->exec();

    if (msg->clickedButton() != delete_btn) {
        return;
    }

    QFile file(model_path);
    if (!file.remove()) {
        showAlertDialog(QStringLiteral("Failed to Delete"),
                        QStringLiteral("Could not delete the file:\n%1").arg(file.errorString()));
        return;
    }

    translate_page_->setStatus(QStringLiteral("Model file deleted."));
    refreshModelPage();
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
    auto *panel = new ModelMissingPanel(
        qtrans::app::from_utf8(paths_.modeLabel()),
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
    connect(download_panel_, &DownloadProgressPanel::cancelRequested, this, [this]() {
        awaiting_download_load_ = false;
        hideModal();
        task_service_->cancelTask(0);
    });
    modal_->setContent(download_panel_, QSize(460, 260));
    modal_->showModal();
}

void MainWindow::showAlertDialog(const QString &title, const QString &message) {
    auto *panel = new AlertPanel(title, message);
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
    const QString &source,
    const QString &target_language,
    const QString &source_language,
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
        Q_ARG(bool, back_translate),
        Q_ARG(bool, false));

    translate_page_->setTranslating(true);
}

void MainWindow::onTranslateTaskStarted(quint64 task_id) {
    qtrans::log::get(qtrans::log::Component::App)
        ->debug(
            "translateTaskStarted task:{} own_active:{}",
            task_id,
            own_translation_active_);
    if (own_translation_active_) {
        active_translate_task_id_ = task_id;
    }
}

void MainWindow::onCancelRequested() {
    qtrans::log::get(qtrans::log::Component::App)
        ->debug("cancelRequested task:{}", active_translate_task_id_);
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
    qtrans::log::get(qtrans::log::Component::App)
        ->debug(
            "translationFinished task:{} active:{} own:{}",
            task_id,
            active_translate_task_id_,
            own_translation_active_);
    if (!isActiveTranslateTask(task_id)) {
        return;
    }

    active_translate_task_id_ = 0;
    own_translation_active_ = false;
    translate_page_->setTranslating(false);
}

void MainWindow::onStatusChanged(const QString &message, bool busy) {
    translate_page_->setStatus(busy ? message + QStringLiteral(" ...") : message);
    setUiBusy(busy);
}

void MainWindow::onTargetReset(quint64 task_id) {
    if (!isActiveTranslateTask(task_id)) {
        return;
    }
    qtrans::log::get(qtrans::log::Component::App)->debug("targetReset task:{}", task_id);
    translate_page_->resetTarget();
}

void MainWindow::onTargetAppended(quint64 task_id, const QString &piece) {
    if (!isActiveTranslateTask(task_id)) {
        return;
    }
    qtrans::log::get(qtrans::log::Component::App)
        ->debug("targetAppended task:{} len:{}", task_id, piece.size());
    translate_page_->appendTarget(piece);
}

void MainWindow::onModelLoadFinished(
    bool success,
    const QString &error_message,
    const QString &backend_label) {
    model_loaded_ = success;
    loaded_model_id_ = success ? qtrans::app::from_utf8(settings_.model_id) : QString{};
    translate_page_->setModelLoaded(success);
    model_page_->setModelLoaded(success);
    model_page_->setLoadedModelId(loaded_model_id_);
    setUiBusy(busy_);

    if (success) {
        if (!backend_label.isEmpty()) {
            translate_page_->setStatus(backend_label);
        }
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
    loaded_model_id_.clear();
    translate_page_->setModelLoaded(false);
    model_page_->setModelLoaded(false);
    model_page_->setLoadedModelId({});
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

void MainWindow::onBackTranslateAppended(quint64 task_id, const QString &piece) {
    if (!isActiveTranslateTask(task_id)) {
        return;
    }
    translate_page_->appendBackTranslate(piece);
}

// ── Batch UI slots ───────────────────────────────────────────────────────────
// All UI→worker calls use QueuedConnection since BatchController lives on the
// worker thread alongside TaskService.

void MainWindow::onBatchAddFiles(const QString &source_lang,
                                 const QString &target_lang) {
    hideModal();
    const QStringList file_paths = QFileDialog::getOpenFileNames(
        this,
        QStringLiteral("Select file(s) for batch translation"),
        QString(),
        QStringLiteral("Text files (*.txt *.md *.srt);;All files (*)"));
    if (file_paths.isEmpty()) return;

    for (const QString &path : file_paths) {
        QMetaObject::invokeMethod(
            batch_controller_, "addFile",
            Qt::QueuedConnection,
            Q_ARG(QString, path),
            Q_ARG(QString, source_lang),
            Q_ARG(QString, target_lang));
    }
}

void MainWindow::onBatchShowLanguagePicker() {
    batch_lang_panel_ = new BatchLangPanel();
    batch_lang_panel_->setDefaultLanguages(
        translate_page_->sourceLanguageName(),
        translate_page_->targetLanguageName());
    connect(batch_lang_panel_, &BatchLangPanel::confirmed,
            this, &MainWindow::onBatchAddFiles);
    connect(batch_lang_panel_, &BatchLangPanel::cancelled,
            this, &MainWindow::onBatchLanguagePickerCancelled);
    modal_->setContent(batch_lang_panel_, QSize(460, 260));
    modal_->showModal();
}

void MainWindow::onBatchLanguagePickerCancelled() {
    hideModal();
}

void MainWindow::onBatchRemoveEntry(const QStringList &entry_ids) {
    for (const QString &id : entry_ids) {
        QMetaObject::invokeMethod(
            batch_controller_, "removeEntry",
            Qt::QueuedConnection,
            Q_ARG(QString, id));
    }
}

void MainWindow::onBatchStart() {
    batch_page_->setRunning(true);
    batch_page_->setStatusText(QStringLiteral("Batch running..."));
    QMetaObject::invokeMethod(batch_controller_, "start", Qt::QueuedConnection);
}

void MainWindow::onBatchPause() {
    batch_page_->setPaused(true);
    batch_page_->setStatusText(QStringLiteral("Batch paused"));
    QMetaObject::invokeMethod(batch_controller_, "pause", Qt::QueuedConnection);
}

void MainWindow::onBatchResume() {
    batch_page_->setPaused(false);
    batch_page_->setStatusText(QStringLiteral("Batch running..."));
    QMetaObject::invokeMethod(batch_controller_, "resume", Qt::QueuedConnection);
}

void MainWindow::onBatchEntryAdded(const QString &entry_id,
                                   const QString &source_language,
                                   const QString &target_language) {
    QString file_name;
    QMetaObject::invokeMethod(
        batch_controller_, "entryFileName",
        Qt::BlockingQueuedConnection,
        Q_RETURN_ARG(QString, file_name),
        Q_ARG(QString, entry_id));
    batch_page_->addCard(entry_id, file_name, source_language, target_language);

    // Check if already saved (e.g. restored from persisted queue).
    QVariantMap meta;
    QMetaObject::invokeMethod(
        batch_controller_, "entryMetadata",
        Qt::BlockingQueuedConnection,
        Q_RETURN_ARG(QVariantMap, meta),
        Q_ARG(QString, entry_id));
    if (meta.value(QStringLiteral("saved")).toBool()) {
        batch_page_->setCardSaved(entry_id,
                                  meta.value(QStringLiteral("save_path")).toString());
    }
}

void MainWindow::onBatchEntryRemoved(const QString &entry_id) {
    batch_page_->removeCard(entry_id);
}

void MainWindow::onBatchEntryStateChanged(const QString &entry_id, int state) {
    batch_page_->setCardState(entry_id, state);
}

void MainWindow::onBatchSegmentProgress(const QString &entry_id, int completed, int total) {
    batch_page_->setCardProgress(entry_id, completed, total);
}

void MainWindow::onBatchSaveEntry(const QStringList &entry_ids) {
    if (entry_ids.isEmpty()) return;

    const QString dest_dir = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("Select destination for translated files"),
        QString());
    if (dest_dir.isEmpty()) return;

    QMetaObject::invokeMethod(
        batch_controller_, "saveEntriesToDirectory",
        Qt::QueuedConnection,
        Q_ARG(QStringList, entry_ids),
        Q_ARG(QString, dest_dir));
    batch_page_->setStatusText(
        QStringLiteral("Saving %1 file(s)...").arg(entry_ids.size()));
}

void MainWindow::onBatchError(const QString &message) {
    batch_page_->setStatusText(QStringLiteral("Error: ") + message);
    qtrans::log::get(qtrans::log::Component::App)->error("batch error: {}", qtrans::app::to_utf8(message));
}
