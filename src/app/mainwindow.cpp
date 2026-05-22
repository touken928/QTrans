#include "app/mainwindow.h"

#include "app/backend.h"
#include "app/modal_overlay.h"
#include "app/ui/app_theme.h"
#include "app/ui/download_progress_panel.h"
#include "app/ui/model_missing_panel.h"
#include "app/ui/model_page.h"
#include "app/ui/sidebar_widget.h"
#include "app/ui/translate_page.h"
#include "download/download.h"
#include "settings/model_catalog.h"

#include <QCoreApplication>
#include <QHBoxLayout>
#include <QMetaObject>
#include <QShowEvent>
#include <QStackedWidget>
#include <QThread>

MainWindow::MainWindow(Backend * backend, QThread * worker_thread, QWidget * parent)
    : QMainWindow(parent)
    , backend_(backend)
    , worker_thread_(worker_thread)
    , paths_(AppPaths::detect(QCoreApplication::applicationDirPath().toStdString())) {
    setWindowTitle(QStringLiteral("QTrans"));
    resize(960, 600);
    setMinimumSize(720, 480);

    paths_.ensureDirectories();
    settings_.load(paths_);
    settings_.ensureStorage(paths_);
    syncSettingsToBackend();

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

    connect(backend_, &Backend::statusChanged, this, &MainWindow::onStatusChanged);
    connect(backend_, &Backend::modelLoadFinished, this, &MainWindow::onModelLoadFinished);
    connect(backend_, &Backend::modelUnloadFinished, this, &MainWindow::onModelUnloadFinished);
    connect(backend_, &Backend::downloadProgress, this, &MainWindow::onDownloadProgress);
    connect(backend_, &Backend::downloadFinished, this, &MainWindow::onDownloadFinished);
    connect(backend_, &Backend::targetReset, this, &MainWindow::onTargetReset);
    connect(backend_, &Backend::targetAppended, this, &MainWindow::onTargetAppended);
    connect(backend_, &Backend::backTranslateReset, this, &MainWindow::onBackTranslateReset);
    connect(backend_, &Backend::backTranslateAppended, this, &MainWindow::onBackTranslateAppended);
    connect(backend_, &Backend::translationFinished, this, &MainWindow::onTranslationFinished);
}

MainWindow::~MainWindow() {
    if (worker_thread_ != nullptr) {
        worker_thread_->quit();
        worker_thread_->wait();
    }
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
    syncSettingsToBackend();
}

QString MainWindow::currentModelPath() const {
    return QString::fromStdString(settings_.effectiveModelPath(paths_));
}

void MainWindow::syncSettingsToBackend() {
    const ModelCatalogEntry * model = settings_.selectedModel();
    backend_->setModelPath(currentModelPath());
    backend_->setRemoteSpec(QString::fromStdString(model->remote_spec));
    backend_->setDownloadHub(model->download_hub);
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
    syncSettingsToBackend();
    refreshModelPage();
    translate_page_->setStatus(QStringLiteral("Model settings saved"));
}

void MainWindow::onLoadModelFromPage() {
    applySettingsFromPage();
    settings_.ensureStorage(paths_);
    saveSettings();
    syncSettingsToBackend();
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

    QMetaObject::invokeMethod(backend_, "unloadModel", Qt::QueuedConnection);
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

void MainWindow::startDownloadAndLoad() {
    awaiting_download_load_ = true;
    showDownloadDialog();
    QMetaObject::invokeMethod(backend_, "downloadModel", Qt::QueuedConnection);
}

void MainWindow::startLoadModel() {
    syncSettingsToBackend();
    QMetaObject::invokeMethod(backend_, "loadModel", Qt::QueuedConnection);
}

void MainWindow::onTranslateRequested(
    const QString & source,
    const QString & target_language,
    const QString & source_language,
    bool back_translate) {
    pending_back_translate_ = back_translate;
    pending_back_language_ = source_language;
    pending_forward_translation_.clear();

    QMetaObject::invokeMethod(
        backend_,
        "translate",
        Qt::QueuedConnection,
        Q_ARG(QString, source),
        Q_ARG(QString, target_language),
        Q_ARG(bool, false),
        Q_ARG(bool, false));
}

void MainWindow::onTranslationFinished() {
    if (!pending_back_translate_) {
        return;
    }

    pending_forward_translation_ = translate_page_->targetText().trimmed();
    pending_back_translate_ = false;

    if (pending_forward_translation_.isEmpty() || pending_back_language_.isEmpty()) {
        return;
    }

    QMetaObject::invokeMethod(
        backend_,
        "translate",
        Qt::QueuedConnection,
        Q_ARG(QString, pending_forward_translation_),
        Q_ARG(QString, pending_back_language_),
        Q_ARG(bool, true),
        Q_ARG(bool, true));
}

void MainWindow::onStatusChanged(const QString & message, bool busy) {
    translate_page_->setStatus(busy ? message + QStringLiteral(" ...") : message);
    setUiBusy(busy);
}

void MainWindow::onModelLoadFinished(bool success) {
    model_loaded_ = success;
    translate_page_->setModelLoaded(success);
    model_page_->setModelLoaded(success);
    setUiBusy(busy_);

    if (success && modal_->isVisible()) {
        hideModal();
        switchPage(0);
    }
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

void MainWindow::onTargetReset() {
    translate_page_->resetTarget();
}

void MainWindow::onTargetAppended(const QString & piece) {
    translate_page_->appendTarget(piece);
}

void MainWindow::onBackTranslateReset() {
    translate_page_->resetBackTranslate();
}

void MainWindow::onBackTranslateAppended(const QString & piece) {
    translate_page_->appendBackTranslate(piece);
}
