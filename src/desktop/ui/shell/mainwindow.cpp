#include "ui/shell/mainwindow.h"

#include "ui/shell/model_unavailable_banner.h"
#include "ui/shell/preferences_page.h"
#include "ui/shell/shell_status_bar.h"
#include "ui/shared/modal_overlay.h"
#include "app/batch_controller.h"
#include "app/download_service.h"
#include "app/inference_service.h"
#include "app/local_api_service.h"
#include "ui/shared/theme/app_theme.h"
#include "ui/shared/panels/alert_panel.h"
#include "ui/shared/panels/download_progress_panel.h"
#include "ui/pages/batch/batch_page.h"
#include "ui/pages/models/model_page.h"
#include "ui/sidebar/sidebar_widget.h"
#include "ui/pages/translate/translate_page.h"
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
#include <QFile>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QMetaObject>
#include <QPushButton>
#include <QShowEvent>
#include <QStackedWidget>
#include <QThread>
#include <QVBoxLayout>

#include <algorithm>

MainWindow::MainWindow(
    InferenceService *inference_service,
    DownloadService *download_service,
    BatchController *batch_controller,
    LocalApiService *local_api_service,
    QThread *worker_thread,
    const AppPaths &paths,
    QWidget *parent)
    : QMainWindow(parent),
      inference_service_(inference_service),
      download_service_(download_service),
      batch_controller_(batch_controller),
      local_api_service_(local_api_service),
      worker_thread_(worker_thread),
      paths_(paths) {
    setWindowTitle(QStringLiteral("QTrans"));
    resize(960, 600);
    setMinimumSize(720, 480);

    settings_.load(paths_);
    settings_.ensureStorage(paths_);
    initializeInferenceBackend();
    settings_.migrateModelSelection(RuntimeCapabilities::instance());
    syncSettingsToServices();

    central_root_ = new QWidget(this);
    central_root_->setObjectName(QStringLiteral("centralRoot"));
    // Root stacks the shell row above the full-width operational band.
    auto *root = new QVBoxLayout(central_root_);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Shell row: sidebar + content column ───────────────────────────
    auto *shell = new QHBoxLayout();
    shell->setContentsMargins(0, 0, 0, 0);
    shell->setSpacing(0);

    sidebar_ = new SidebarWidget(central_root_);
    shell->addWidget(sidebar_);

    // ── Content column: banner + page stack ───────────────────────────
    content_column_ = new QWidget(central_root_);
    auto *column = new QVBoxLayout(content_column_);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);

    model_banner_ = new ModelUnavailableBanner(content_column_);
    column->addWidget(model_banner_);

    translate_page_ = new TranslatePage(content_column_);
    model_page_ = new ModelPage(content_column_);
    model_page_->setSettings(paths_, settings_);
    batch_page_ = new BatchPage(content_column_);

    // Preferences owns its settings controls directly; Word Select is a
    // section inside it, not a hosted page.
    preferences_page_ = new PreferencesPage(content_column_);

    content_stack_ = new QStackedWidget(content_column_);
    content_stack_->addWidget(translate_page_);    // PageId::Translate
    content_stack_->addWidget(batch_page_);        // PageId::Documents
    content_stack_->addWidget(model_page_);        // PageId::Models
    content_stack_->addWidget(preferences_page_);  // PageId::Preferences
    column->addWidget(content_stack_, 1);

    shell->addWidget(content_column_, 1);
    root->addLayout(shell, 1);

    // ── Full-width operational band, below the shell row ──────────────
    // The status bar spans the complete window width — including under the
    // sidebar — so it reads as the workbench's bottom edge, not a piece of
    // the content column.
    status_bar_ = new ShellStatusBar(central_root_);
    root->addWidget(status_bar_);

    setCentralWidget(central_root_);
    AppTheme::apply(this);

    modal_ = new ModalOverlay(central_root_);
    switchPage(PageId::Translate);

    connect(sidebar_, &SidebarWidget::pageSelected, this, &MainWindow::onPageSelected);

    // ── Unavailable-model banner (nonmodal, shell-controlled) ─────────
    connect(model_banner_, &ModelUnavailableBanner::downloadRequested,
            this, &MainWindow::startDownloadAndLoad);
    connect(model_banner_, &ModelUnavailableBanner::loadRequested,
            this, &MainWindow::startLoadModel);
    connect(model_banner_, &ModelUnavailableBanner::modelsRequested,
            this, [this]() { switchPage(PageId::Models); });
    connect(model_banner_, &ModelUnavailableBanner::dismissed,
            this, [this]() {
                // Dismissal is scoped to this configured model + availability
                // episode (file missing vs present), so a later, different
                // need for the same or another model still surfaces.
                dismissed_banner_model_id_ = qtrans::app::from_utf8(settings_.model_id);
                dismissed_banner_file_missing_ =
                    !download_file_exists(settings_.effectiveModelPath(paths_));
                model_banner_->hide();
            });

    connect(model_page_, &ModelPage::loadModelRequested, this, [this](const QString &model_id) {
        settings_.setSelectedModelId(qtrans::app::to_utf8(model_id));
        applySettingsFromPage();
        onLoadModelFromPage();
    });
    connect(model_page_, &ModelPage::downloadModelRequested,
            this, &MainWindow::onDownloadModelFromPage);
    connect(model_page_, &ModelPage::unloadModelRequested, this, [this](const QString &model_id) {
        if (model_id == loaded_model_id_ || model_id.isEmpty()) {
            onUnloadModelFromPage();
        }
    });
    connect(model_page_, &ModelPage::deleteModelRequested, this, [this](const QString &model_id) {
        onDeleteModelForId(model_id);
    });
    connect(model_page_, &ModelPage::cancelDownloadRequested, this, [this]() {
        // Cancelling is only ever offered for the single correlated active
        // download; stale ids cannot reach this slot.
        if (active_download_id_.is_valid()) {
            awaiting_download_load_ = false;
            hideModal();
            download_service_->cancel(active_download_id_);
        }
    });
    connect(model_page_, &ModelPage::modelEdited, this, &MainWindow::applySettingsFromPage);
    connect(translate_page_, &TranslatePage::translateRequested, this, &MainWindow::onTranslateRequested);
    connect(translate_page_, &TranslatePage::cancelRequested, this, &MainWindow::onCancelRequested);
    connect(translate_page_, &TranslatePage::languageChanged, this, &MainWindow::onLanguageChanged);
    connect(inference_service_, &InferenceService::statusChanged, this, &MainWindow::onStatusChanged);
    connect(inference_service_, &InferenceService::modelLoadFinished, this, &MainWindow::onModelLoadFinished);
    connect(inference_service_, &InferenceService::modelUnloadFinished, this, &MainWindow::onModelUnloadFinished);
    connect(inference_service_, &InferenceService::translationStarted, this, &MainWindow::onTranslationStarted);
    connect(inference_service_, &InferenceService::translationReset, this, &MainWindow::onTranslationReset);
    connect(inference_service_, &InferenceService::translationDelta, this, &MainWindow::onTranslationDelta);
    connect(inference_service_, &InferenceService::translationFinished, this, &MainWindow::onTranslationFinished);
    connect(download_service_, &DownloadService::downloadStarted, this, &MainWindow::onDownloadStarted);
    connect(download_service_, &DownloadService::downloadProgress, this, &MainWindow::onDownloadProgress);
    connect(download_service_, &DownloadService::downloadFinished, this, &MainWindow::onDownloadFinished);

    // ── Batch page wiring (batch controller lives on worker thread;
    //     use queued invocations for UI→worker calls) ─────────────────────
    connect(batch_page_, &BatchPage::addFilesRequested,
            this, &MainWindow::onBatchAddFiles);
    connect(batch_page_, &BatchPage::removeRequested,
            this, &MainWindow::onBatchRemoveEntry);
    connect(batch_page_, &BatchPage::retryRequested,
            this, &MainWindow::onBatchRetry);
    connect(batch_page_, &BatchPage::startRequested,
            this, &MainWindow::onBatchStart);
    connect(batch_page_, &BatchPage::pauseRequested,
            this, &MainWindow::onBatchPause);
    connect(batch_page_, &BatchPage::resumeRequested,
            this, &MainWindow::onBatchResume);

    // BatchController signals (emitted from worker thread) → page updates
    // on UI thread. Auto-connection becomes queued cross-thread. The table
    // is driven exclusively by the complete queue snapshot — no per-entry
    // blocking round trips on the UI thread.
    connect(batch_controller_, &BatchController::queueSnapshot,
            this, [this](const QVariantList &entries) {
                batch_page_->setEntries(entries);
            });
    connect(batch_controller_, &BatchController::batchStateChanged,
            this, [this](bool running, bool paused) {
                batch_running_ = running;
                batch_paused_ = paused;
                batch_page_->setRunning(running);
                batch_page_->setPaused(paused);
                projectShellState();
            });
    connect(batch_controller_, &BatchController::batchFinished,
            this, [this]() {
                batch_running_ = false;
                batch_paused_ = false;
                batch_page_->setRunning(false);
                batch_page_->setStatusText(QStringLiteral("Batch finished"));
                projectShellState();
            });
    connect(batch_controller_, &BatchController::errorOccurred,
            this, &MainWindow::onBatchError);

    // Load persisted queue entries so the UI auto-syncs.
    QMetaObject::invokeMethod(batch_controller_, "loadPersistedEntries",
                              Qt::QueuedConnection);

    hotkey_manager_ = new HotkeyManager(this);

    popup_window_ = new PopupWindow(nullptr);

    session_controller_ = new SessionController(
        hotkey_manager_, inference_service_, popup_window_, this);
    // Note: the popup's retryRequested is connected once, inside
    // SessionController's constructor — the session is the sole owner of
    // the retry flow.
    session_controller_->initialize();
    // Word-select source is always Auto (runtime auto-detects the selected
    // text's language); the target comes from the persisted word-select
    // target, and the popup honors the persisted auto-close interval.
    session_controller_->setTranslateLanguages(
        QStringLiteral("Auto"),
        qtrans::app::from_utf8(settings_.wordselect_target_language));
    popup_window_->setAutoCloseMs(settings_.auto_close_ms);
    session_controller_->setEnabled(settings_.wordselect_enabled);

    translate_page_->setSourceLanguage(qtrans::app::from_utf8(settings_.source_language));
    translate_page_->setTargetLanguage(qtrans::app::from_utf8(settings_.target_language));
    syncLanguagesToSettings();
    batch_page_->setDefaultLanguages(translate_page_->sourceLanguageName(),
                                     translate_page_->targetLanguageName());

    preferences_page_->setEnabled(settings_.wordselect_enabled);
    preferences_page_->setCloseToTray(settings_.close_to_tray);
    preferences_page_->setTargetLanguage(qtrans::app::from_utf8(settings_.wordselect_target_language));
    preferences_page_->setAutoCloseMs(settings_.auto_close_ms);
    preferences_page_->setApiEnabled(settings_.api_enabled);
    preferences_page_->setApiPort(settings_.api_port);
    preferences_page_->setDataDirectory(
        qtrans::app::from_utf8(paths_.data_root.string()));
    connect(preferences_page_, &PreferencesPage::settingsChanged,
            this, &MainWindow::onWordSelectSettingsChanged);

    system_tray_ = new SystemTray(this);
    connect(system_tray_, &SystemTray::openMainWindow, this, &MainWindow::bringToForeground);
    connect(system_tray_, &SystemTray::toggleTranslation,
            session_controller_, &SessionController::setEnabled);
    connect(system_tray_, &SystemTray::quitApp, qApp, &QCoreApplication::quit);

    // Apply the persisted shortcut last, once the preferences page and the
    // tray can surface a registration failure. A shortcut that cannot be
    // registered is never persisted; the page rolls back to what actually
    // works.
    const QString persisted_hotkey = qtrans::app::from_utf8(settings_.hotkey);
    if (!persisted_hotkey.isEmpty() &&
        !session_controller_->setHotkey(persisted_hotkey)) {
        const QString active_hotkey = session_controller_->hotkey();
        // A failed registration must never overwrite the previously
        // configured shortcut with an empty persisted value: hotkey() only
        // reports empty when the rollback also failed, and persisting that
        // would strand the preferences page on an empty field next launch.
        if (!active_hotkey.isEmpty()) {
            settings_.hotkey = qtrans::app::to_utf8(active_hotkey);
            settings_.save(paths_);
        } else {
            qtrans::log::get(qtrans::log::Component::App)
                ->warn(
                    "hotkey '{}' failed to register and rollback left no "
                    "active binding; keeping persisted shortcut '{}'",
                    qtrans::app::to_utf8(persisted_hotkey),
                    settings_.hotkey);
        }
        preferences_page_->setHotkey(active_hotkey);
        preferences_page_->setFeedback(
            active_hotkey.isEmpty()
                ? QStringLiteral(
                      "Could not register the saved shortcut \u201C%1\u201D \u2014 "
                      "it may already be in use. No shortcut is currently active.")
                      .arg(persisted_hotkey)
                : QStringLiteral(
                      "Could not register the saved shortcut \u201C%1\u201D \u2014 "
                      "it may already be in use. Reverted to %2.")
                      .arg(persisted_hotkey, active_hotkey),
            true);
        system_tray_->showMessage(
            QStringLiteral("QTrans"),
            QStringLiteral("Shortcut \u201C%1\u201D could not be registered.").arg(persisted_hotkey),
            QSystemTrayIcon::Warning, 4000);
    } else {
        preferences_page_->setHotkey(persisted_hotkey);
    }

    projectShellState();

    // Enable the persisted local API service once the UI is fully wired.
    syncApiService();
}

MainWindow::~MainWindow() = default;

void MainWindow::bringToForeground() {
    show();
    raise();
    activateWindow();
}

LocalApiService *MainWindow::localApiService() const {
    return local_api_service_;
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

void MainWindow::onPageSelected(PageId page) {
    switchPage(page);
}

void MainWindow::switchPage(PageId page) {
    const int index = stackIndexOfPage(page);
    if (index < 0) {
        // Invalid/Count page id: fail safe by keeping the current page
        // (stackIndexOfPage asserts in debug builds).
        return;
    }
    content_stack_->setCurrentIndex(index);
    sidebar_->setCurrentPage(page);

    if (page == PageId::Models) {
        refreshModelPage();
    }
}

void MainWindow::refreshModelPage() {
    model_page_->setRuntimeCapabilities(RuntimeCapabilities::instance());
    model_page_->setSettings(paths_, settings_);
    model_page_->setModelLoaded(model_loaded_);
    model_page_->setLoadedModelId(loaded_model_id_);
    model_page_->setLoadingModelId(loading_model_id_);
    model_page_->setUnloading(unloading_);
    model_page_->setDownloadingModelId(active_download_model_id_);
    if (active_download_id_.is_valid()) {
        model_page_->setDownloadProgress(last_download_done_, last_download_total_);
    }
}

void MainWindow::initializeInferenceBackend() {
    inference_service_->initializeBackend();
}

void MainWindow::applySettingsFromPage() {
    model_page_->applyTo(settings_);
    syncSettingsToServices();
}

QString MainWindow::currentModelPath() const {
    return qtrans::app::from_utf8(settings_.effectiveModelPath(paths_));
}

void MainWindow::syncSettingsToServices() {
    const ModelCatalogEntry *model = settings_.selectedModel();
    inference_service_->setModelConfig(qtrans::app::from_utf8(model->id), currentModelPath());
    DownloadRequest request;
    request.local_path = qtrans::app::to_utf8(currentModelPath());
    request.remote_spec = model->remote_spec;
    request.modelscope_remote_spec = model->modelscope_remote_spec;
    request.download_hub = model->download_hub;
    download_service_->setDownloadRequest(request);
}

void MainWindow::syncApiService() {
    if (!settings_.api_enabled) {
        local_api_service_->stop();
        return;
    }
    // settings_.api_port is validated on load and by the UI spinbox
    // (1024..65535); clamp defensively so a corrupted value can never wrap a
    // signed int into an invalid quint16 listener port.
    const int port = std::clamp(settings_.api_port, 1024, 65535);
    QString error;
    const bool started = local_api_service_->start(static_cast<quint16>(port), &error);
    if (!started) {
        // The Translate page owns no local status surface; the failure is
        // kept in the app log instead.
        qtrans::log::get(qtrans::log::Component::App)
            ->error("local API failed to start on port {}: {}", settings_.api_port,
                    qtrans::app::to_utf8(error));
    }
}

void MainWindow::syncLanguagesToSettings() {
    const QString source = translate_page_->sourceLanguageName();
    const QString target = translate_page_->targetLanguageName();
    settings_.source_language = qtrans::app::to_utf8(source);
    settings_.target_language = qtrans::app::to_utf8(target);
    saveSettings();
}

void MainWindow::onWordSelectSettingsChanged() {
    const bool enabled = preferences_page_->isEnabled();
    const bool close_to_tray = preferences_page_->isCloseToTray();
    const QString target = preferences_page_->targetLanguage();
    const QString hotkey = preferences_page_->hotkey();
    const int auto_close = preferences_page_->autoCloseMs();
    const bool api_enabled = preferences_page_->isApiEnabled();
    const int api_port = preferences_page_->apiPort();

    // Validate the shortcut before touching any live or persisted state: a
    // shortcut that cannot be registered must not be saved and must not
    // displace the one that is currently working. SessionController rolls
    // the registration back internally and reports the binding that is
    // actually active; here the UI and settings follow that report.
    QString effective_hotkey = hotkey;
    if (hotkey.isEmpty()) {
        // The preferences page pre-validates shape, but stay defensive.
        preferences_page_->setFeedback(
            QStringLiteral("A shortcut with a modifier key is required, e.g. Ctrl+`."),
            true);
        effective_hotkey = session_controller_->hotkey();
        preferences_page_->setHotkey(effective_hotkey);
    } else if (!session_controller_->setHotkey(hotkey)) {
        effective_hotkey = session_controller_->hotkey();
        preferences_page_->setHotkey(effective_hotkey);
        preferences_page_->setFeedback(
            effective_hotkey.isEmpty()
                ? QStringLiteral("Could not register shortcut \u201C%1\u201D \u2014 "
                                 "it may already be in use. No shortcut is currently active.")
                      .arg(hotkey)
                : QStringLiteral("Could not register shortcut \u201C%1\u201D \u2014 "
                                 "it may already be in use. Reverted to %2.")
                      .arg(hotkey, effective_hotkey),
            true);
        system_tray_->showMessage(
            QStringLiteral("QTrans"),
            QStringLiteral("Shortcut \u201C%1\u201D could not be registered.")
                .arg(hotkey),
            QSystemTrayIcon::Warning, 4000);
    } else {
        preferences_page_->setFeedback(QString{}, false);
    }

    settings_.wordselect_enabled = enabled;
    settings_.close_to_tray = close_to_tray;
    settings_.wordselect_target_language = qtrans::app::to_utf8(target);
    // A failed registration must never overwrite the previously configured
    // hotkey with an empty persisted value; hotkey() only reports empty when
    // the rollback also failed, so keep the last known-good value instead.
    if (!effective_hotkey.isEmpty()) {
        settings_.hotkey = qtrans::app::to_utf8(effective_hotkey);
    } else {
        qtrans::log::get(qtrans::log::Component::App)
            ->warn(
                "hotkey '{}' failed to register and rollback left no "
                "active binding; keeping persisted shortcut '{}'",
                qtrans::app::to_utf8(hotkey),
                settings_.hotkey);
    }
    settings_.auto_close_ms = auto_close;
    settings_.api_enabled = api_enabled;
    settings_.api_port = api_port;
    saveSettings();

    session_controller_->setEnabled(enabled);
    session_controller_->setTranslateLanguages(QStringLiteral("Auto"), target);
    popup_window_->setAutoCloseMs(auto_close);
    syncApiService();
}

void MainWindow::saveSettings() {
    try {
        settings_.save(paths_);
    } catch (const std::exception &ex) {
        qtrans::log::get(qtrans::log::Component::App)
            ->error("failed to save settings: {}", ex.what());
    }
}

void MainWindow::onSaveModelSettings() {
    applySettingsFromPage();
    settings_.ensureStorage(paths_);
    saveSettings();
    syncSettingsToServices();
    refreshModelPage();
    projectShellState();
}

void MainWindow::onLoadModelFromPage() {
    applySettingsFromPage();
    settings_.ensureStorage(paths_);
    saveSettings();
    syncSettingsToServices();
    refreshModelPage();
    projectShellState();

    // The user explicitly asked to load this model: any earlier dismissal
    // for it no longer applies, so a missing file reopens the download path.
    dismissed_banner_model_id_.clear();

    if (download_file_exists(settings_.effectiveModelPath(paths_))) {
        startLoadModel();
        return;
    }

    // The file is missing: surface the nonmodal banner instead of blocking
    // the whole window; the user can download or pick another model.
    refreshModelAvailability();
}

void MainWindow::onUnloadModelFromPage() {
    if (!model_loaded_) {
        return;
    }
    // Belt and braces behind the page-level gating: an unload must never
    // start while a live interactive or batch job owns the model.
    if (own_translation_active_ || batch_running_) {
        return;
    }

    unloading_ = true;
    model_page_->setUnloading(true);
    inference_service_->unloadModel();
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

    refreshModelPage();
    refreshModelAvailability();
}

void MainWindow::setUiBusy(bool busy) {
    busy_ = busy;
    // Lifecycle work must not prevent page navigation, so the sidebar stays
    // enabled. Conflicting actions are still guarded at the page level:
    // translate controls disable themselves while busy, and the model page
    // gates every row action from the granular lifecycle state pushed by
    // this window (loading id / unloading / correlated download).
    translate_page_->setBusy(busy);
    translate_page_->setModelLoaded(model_loaded_);
}

void MainWindow::performStartupCheck() {
    if (download_file_exists(settings_.effectiveModelPath(paths_))) {
        startLoadModel();
        return;
    }

    // Model file missing: show the nonmodal banner instead of blocking the
    // window at startup. The banner keeps an actionable path to download or
    // to the Models page, and all pages stay reachable.
    refreshModelAvailability();
}

void MainWindow::hideModal() {
    // Whatever modal is up, it is no longer the model-flow download panel.
    model_flow_modal_active_ = false;
    modal_->hideModal();
}

void MainWindow::showDownloadDialog() {
    download_panel_ = new DownloadProgressPanel();
    connect(download_panel_, &DownloadProgressPanel::cancelRequested, this, [this]() {
        awaiting_download_load_ = false;
        hideModal();
        if (active_download_id_.is_valid()) {
            download_service_->cancel(active_download_id_);
        }
    });
    // Remember that the visible modal belongs to the model flow so a load
    // result only ever closes/downloads for this dialog, never an unrelated
    // modal (alert, batch language picker, ...).
    model_flow_modal_active_ = true;
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
    const DownloadId id = download_service_->startDownload();
    // The reserved id (accepted or promptly-rejected) is bound to the model
    // whose file this request writes before this slot returns, so the
    // download is active from reservation time and no second request can
    // race the queued downloadStarted event.
    if (id.is_valid()) {
        bindActiveDownload(id, qtrans::app::from_utf8(settings_.model_id));
    }
}

void MainWindow::bindActiveDownload(DownloadId id, const QString &model_id) {
    active_download_id_ = id;
    active_download_model_id_ = model_id;
    download_active_ = true;
    model_page_->setDownloadingModelId(model_id);
    refreshModelAvailability();
    projectShellState();
}

void MainWindow::startLoadModel() {
    syncSettingsToServices();
    loading_model_id_ = qtrans::app::from_utf8(settings_.model_id);
    model_page_->setLoadingModelId(loading_model_id_);
    inference_service_->loadModel();
}

void MainWindow::onDownloadModelFromPage(const QString &model_id) {
    // Explicit download request from the model library: configure the model,
    // persist, and start the single download with inline row progress (no
    // modal — ordinary progress stays in the page). Only one download can
    // ever be accepted; the synchronous binding below makes the window
    // between reservation and downloadStarted impossible to race.
    if (download_active_) {
        return;
    }
    settings_.setSelectedModelId(qtrans::app::to_utf8(model_id));
    applySettingsFromPage();
    settings_.ensureStorage(paths_);
    saveSettings();
    syncSettingsToServices();
    refreshModelPage();
    projectShellState();
    dismissed_banner_model_id_.clear();

    awaiting_download_load_ = true;
    const DownloadId id = download_service_->startDownload();
    if (id.is_valid()) {
        bindActiveDownload(id, model_id);
    }
}

void MainWindow::onTranslateRequested(
    const QString &source,
    const QString &target_language,
    const QString &source_language,
    bool back_translate) {
    active_translate_job_id_ = TranslationJobId{};
    own_translation_active_ = true;

    NativeTranslationRequest request;
    request.source = qtrans::app::to_utf8(source);
    request.target_language = qtrans::app::to_utf8(target_language);
    request.source_language = qtrans::app::to_utf8(source_language);
    request.back_translate = back_translate;
    request.wordselect = false;
    active_translate_job_id_ = inference_service_->translateNative(request);

    translate_page_->setTranslating(true);
    projectShellState();
}

void MainWindow::onTranslationStarted(TranslationJobId job_id) {
    // The active job id is the synchronously returned value from
    // translateNative(); a started event from an unrelated (popup/batch) job
    // must never overwrite it.
    if (!isActiveTranslateJob(job_id)) {
        return;
    }
    qtrans::log::get(qtrans::log::Component::App)
        ->debug("translationStarted job:{} own_active:{}", job_id.value,
                own_translation_active_);
}

void MainWindow::onCancelRequested() {
    qtrans::log::get(qtrans::log::Component::App)
        ->debug("cancelRequested job:{}", active_translate_job_id_.value);
    if (active_translate_job_id_.is_valid()) {
        inference_service_->cancel(active_translate_job_id_);
    }
}

void MainWindow::onLanguageChanged() {
    syncLanguagesToSettings();
    // New batch enqueues follow the Translate page defaults; queued entries
    // keep the language snapshot they were added with.
    batch_page_->setDefaultLanguages(translate_page_->sourceLanguageName(),
                                     translate_page_->targetLanguageName());
}

bool MainWindow::isActiveTranslateJob(TranslationJobId job_id) const {
    return active_translate_job_id_.is_valid() && active_translate_job_id_ == job_id;
}

void MainWindow::onTranslationFinished(const TranslationJobResult &result) {
    qtrans::log::get(qtrans::log::Component::App)
        ->debug(
            "translationFinished job:{} active:{} own:{}",
            result.id.value,
            active_translate_job_id_.value,
            own_translation_active_);
    if (!isActiveTranslateJob(result.id)) {
        return;
    }

    active_translate_job_id_ = TranslationJobId{};
    own_translation_active_ = false;
    translate_page_->setTranslating(false);

    // Surface the terminal outcome in the result pane: completed,
    // cancelled/preempted, or failed (with the error text kept for Retry).
    QString error_text;
    if (result.state == TranslationState::Failed) {
        error_text = QString::fromStdString(result.error_message).trimmed();
        if (error_text.startsWith(QStringLiteral("Error:"))) {
            error_text = error_text.mid(6).trimmed();
        }
    }
    translate_page_->setTranslationResult(result.state, error_text);
    projectShellState();
}

void MainWindow::onStatusChanged(const QString &message, bool busy) {
    // The message feeds the shell activity projection; the Translate page
    // owns no local status surface to receive it.
    current_status_message_ = message;
    setUiBusy(busy);
    projectShellState();
}

void MainWindow::onTranslationReset(TranslationJobId job_id, TranslationChannel channel) {
    if (!isActiveTranslateJob(job_id)) {
        return;
    }
    qtrans::log::get(qtrans::log::Component::App)
        ->debug("translationReset job:{} channel:{}", job_id.value, static_cast<int>(channel));
    if (channel == TranslationChannel::Target) {
        translate_page_->resetTarget();
    } else {
        translate_page_->resetBackTranslate();
    }
}

void MainWindow::onTranslationDelta(TranslationJobId job_id, TranslationChannel channel,
                                    const QString &piece) {
    if (!isActiveTranslateJob(job_id)) {
        return;
    }
    qtrans::log::get(qtrans::log::Component::App)
        ->debug("translationDelta job:{} len:{}", job_id.value, piece.size());
    if (channel == TranslationChannel::Target) {
        translate_page_->appendTarget(piece);
    } else {
        translate_page_->appendBackTranslate(piece);
    }
}

void MainWindow::onModelLoadFinished(
    bool success,
    const QString &error_message,
    const QString &backend_label) {
    model_loaded_ = success;
    loaded_model_id_ = success ? qtrans::app::from_utf8(settings_.model_id) : QString{};
    load_failed_ = !success;
    loading_model_id_.clear();
    // The backend usage is projected by the bottom status bar (single
    // truthful source: the load result); the Translate page never shows it.
    backend_label_ = success ? backend_label : QString{};
    translate_page_->setModelLoaded(success);
    model_page_->setModelLoaded(success);
    model_page_->setLoadedModelId(loaded_model_id_);
    model_page_->setLoadingModelId({});
    refreshModelAvailability();
    projectShellState();

    if (success) {
        // Close only the modal this window opened for the model flow (the
        // download progress panel). An unrelated modal — alert, batch
        // language picker — must stay up, and the user's current page is
        // preserved: no navigation is forced here.
        if (model_flow_modal_active_) {
            hideModal();
        }
        return;
    }

    // The load the user asked for failed: a previously dismissed banner for
    // this model no longer applies, so the banner reopens the appropriate
    // action (download or load) on top of the error alert.
    dismissed_banner_model_id_.clear();
    refreshModelAvailability();

    QString message = error_message.trimmed();
    if (message.isEmpty()) {
        message = QStringLiteral("Failed to load the model.");
    }
    showAlertDialog(QStringLiteral("Failed to Load Model"), message);
}

void MainWindow::onModelUnloadFinished(bool success, const QString &error_message) {
    // The unload result is terminal in both directions: the lifecycle busy
    // state must always be cleared so the model page and shell never stay
    // locked after a failed unload.
    if (success) {
        model_loaded_ = false;
        loaded_model_id_.clear();
        load_failed_ = false;
        backend_label_.clear();
        translate_page_->setModelLoaded(false);
        model_page_->setModelLoaded(false);
        model_page_->setLoadedModelId({});
    }
    unloading_ = false;
    model_page_->setUnloading(false);
    projectShellState();
    refreshModelAvailability();

    if (!success) {
        const QString message = error_message.trimmed().isEmpty()
                                    ? QStringLiteral("Failed to unload the model.")
                                    : QStringLiteral("Failed to unload the model: %1")
                                          .arg(error_message.trimmed());
        qtrans::log::get(qtrans::log::Component::App)
            ->error("model unload failed: {}", qtrans::app::to_utf8(message));
    }
}

void MainWindow::onDownloadStarted(DownloadId id) {
    // The reserved id was bound synchronously when the request was made
    // (bindActiveDownload), so this event only confirms it. A mismatched id
    // belongs to a stale or superseded lifecycle and must never displace the
    // correlated model binding.
    if (!active_download_id_.is_valid() || id != active_download_id_) {
        return;
    }
    refreshModelAvailability();
    projectShellState();
}

void MainWindow::onDownloadProgress(
    DownloadId id,
    qint64 downloaded,
    qint64 total,
    double speed_bps,
    double eta_seconds) {
    if (!active_download_id_.is_valid() || id != active_download_id_) {
        return;
    }
    last_download_done_ = downloaded;
    last_download_total_ = total;
    model_page_->setDownloadProgress(downloaded, total);
    status_bar_->setDownloadProgress(downloaded, total, speed_bps, eta_seconds);
    if (download_panel_ != nullptr) {
        download_panel_->setProgress(downloaded, total, speed_bps, eta_seconds);
    }
}

void MainWindow::onDownloadFinished(const DownloadResult &result) {
    // Ignore completions from earlier/consecutive downloads.
    if (!active_download_id_.is_valid() || result.id != active_download_id_) {
        return;
    }
    download_active_ = false;
    active_download_id_ = DownloadId{};
    active_download_model_id_.clear();
    last_download_done_ = 0;
    last_download_total_ = 0;
    model_page_->setDownloadingModelId({});
    model_page_->setDownloadProgress(0, 0);
    status_bar_->setDownloadProgress(-1, -1, 0.0, 0.0);
    if (result.state != DownloadState::Completed) {
        awaiting_download_load_ = false;
        if (download_panel_ != nullptr) {
            download_panel_->setFailure();
        }
        refreshModelAvailability();
        projectShellState();
        return;
    }

    if (awaiting_download_load_) {
        awaiting_download_load_ = false;
        if (download_panel_ != nullptr) {
            download_panel_->setLoading();
        }
        startLoadModel();
    }
    refreshModelAvailability();
    projectShellState();
}

// ── Shell state projection ────────────────────────────────────────────────
// The top bar and banner are derived projections of state MainWindow
// already tracks from service signals. No page infers these itself.

QString MainWindow::configuredModelDisplayName() const {
    const ModelCatalogEntry *model = settings_.selectedModel();
    if (model == nullptr) {
        return {};
    }
    return qtrans::app::from_utf8(model->display_name);
}

QString MainWindow::loadedModelDisplayName() const {
    if (!model_loaded_ || loaded_model_id_.isEmpty()) {
        return {};
    }
    const ModelCatalogEntry *entry = find_model_by_id(qtrans::app::to_utf8(loaded_model_id_));
    if (entry == nullptr) {
        return loaded_model_id_;
    }
    return qtrans::app::from_utf8(entry->display_name);
}

void MainWindow::projectShellState() {
    status_bar_->setLoadedModel(loadedModelDisplayName());
    status_bar_->setBackend(backend_label_);

    // Lifecycle actions gate while any inference runs so a load/unload can
    // never displace the model under a live interactive or batch job.
    model_page_->setInferenceActive(own_translation_active_ || batch_running_);

    ShellStatusBar::Activity activity = ShellStatusBar::Activity::Idle;
    QString text = QStringLiteral("No model loaded");

    if (download_active_) {
        activity = ShellStatusBar::Activity::Downloading;
        text = QStringLiteral("Downloading model");
    } else if (busy_) {
        activity = ShellStatusBar::Activity::Loading;
        text = current_status_message_.isEmpty() ? QStringLiteral("Working")
                                                 : current_status_message_;
    } else if (own_translation_active_) {
        activity = ShellStatusBar::Activity::Translating;
        text = QStringLiteral("Translating");
    } else if (batch_running_ && batch_paused_) {
        // Paused is preserved independently of running so the chip can
        // project a distinct paused status instead of "translating".
        activity = ShellStatusBar::Activity::Paused;
        text = QStringLiteral("Batch paused");
    } else if (batch_running_) {
        activity = ShellStatusBar::Activity::Translating;
        text = QStringLiteral("Batch translating");
    } else if (load_failed_) {
        activity = ShellStatusBar::Activity::Failed;
        text = QStringLiteral("Model load failed");
    } else if (model_loaded_) {
        activity = ShellStatusBar::Activity::Ready;
        text = QStringLiteral("Ready");
    }
    status_bar_->setActivity(activity, text);
}

void MainWindow::refreshModelAvailability() {
    const QString configured_id = qtrans::app::from_utf8(settings_.model_id);
    const QString configured_name = configuredModelDisplayName();
    if (configured_id.isEmpty() || configured_name.isEmpty()) {
        model_banner_->hide();
        return;
    }
    const bool file_exists = download_file_exists(settings_.effectiveModelPath(paths_));
    // Relevant whenever the configured model is not the loaded one — even
    // if a different model happens to be loaded, the banner must keep an
    // actionable load/download path for the configured model.
    const bool configured_is_loaded =
        model_loaded_ && !loaded_model_id_.isEmpty() && loaded_model_id_ == configured_id;
    // Dismissal covers one configured model + one availability episode
    // (file missing vs present); any other model or changed episode still
    // surfaces the banner.
    const bool dismissed_this_episode =
        dismissed_banner_model_id_ == configured_id &&
        dismissed_banner_file_missing_ == !file_exists;
    if (busy_ || download_active_ || configured_is_loaded || dismissed_this_episode) {
        model_banner_->hide();
        return;
    }
    model_banner_->setState(!file_exists, configured_name);
    model_banner_->show();
}

// ── Batch UI slots ───────────────────────────────────────────────────────────
// All UI→worker calls use QueuedConnection since BatchController lives on the
// worker thread alongside InferenceService and DownloadService.

void MainWindow::onBatchAddFiles(const QStringList &paths,
                                 const QString &source_lang,
                                 const QString &target_lang) {
    for (const QString &path : paths) {
        QMetaObject::invokeMethod(
            batch_controller_, "addFile",
            Qt::QueuedConnection,
            Q_ARG(QString, path),
            Q_ARG(QString, source_lang),
            Q_ARG(QString, target_lang));
    }
}

void MainWindow::onBatchRemoveEntry(const QStringList &entry_ids) {
    for (const QString &id : entry_ids) {
        QMetaObject::invokeMethod(
            batch_controller_, "removeEntry",
            Qt::QueuedConnection,
            Q_ARG(QString, id));
    }
}

void MainWindow::onBatchRetry(const QStringList &entry_ids) {
    for (const QString &id : entry_ids) {
        QMetaObject::invokeMethod(
            batch_controller_, "retryEntry",
            Qt::QueuedConnection,
            Q_ARG(QString, id));
    }
}

void MainWindow::onBatchStart() {
    batch_running_ = true;
    batch_paused_ = false;
    batch_page_->setRunning(true);
    batch_page_->setStatusText(QStringLiteral("Batch running..."));
    projectShellState();
    QMetaObject::invokeMethod(batch_controller_, "start", Qt::QueuedConnection);
}

void MainWindow::onBatchPause() {
    batch_running_ = true;
    batch_paused_ = true;
    batch_page_->setPaused(true);
    batch_page_->setStatusText(QStringLiteral("Batch paused"));
    projectShellState();
    QMetaObject::invokeMethod(batch_controller_, "pause", Qt::QueuedConnection);
}

void MainWindow::onBatchResume() {
    batch_paused_ = false;
    batch_page_->setPaused(false);
    batch_page_->setStatusText(QStringLiteral("Batch running..."));
    projectShellState();
    QMetaObject::invokeMethod(batch_controller_, "resume", Qt::QueuedConnection);
}

void MainWindow::onBatchError(const QString &message) {
    batch_running_ = false;
    batch_paused_ = false;
    batch_page_->setStatusText(QStringLiteral("Error: ") + message);
    projectShellState();
    qtrans::log::get(qtrans::log::Component::App)->error("batch error: {}", qtrans::app::to_utf8(message));
}
