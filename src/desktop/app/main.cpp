#include "domain/platform/single_instance/single_instance.h"
#include "shared/string_bridge.h"
#include "app/batch_controller.h"
#include "app/download_service.h"
#include "app/inference_service.h"
#include "ui/shell/mainwindow.h"
#include "domain/logging/config.h"
#include "domain/logging/init.h"
#include "domain/storage/app_paths.h"

#include <QApplication>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QMetaObject>
#include <QThread>

#include <filesystem>
#include <memory>
#include <spdlog/common.h>

namespace {

qtrans::log::LogConfig make_log_config(const AppPaths &paths) {
    qtrans::log::LogConfig config;
    config.logs_dir = paths.logs_dir;
#ifdef NDEBUG
    config.console_level = spdlog::level::warn;
    config.enable_file_sink = false;
#else
    config.console_level = spdlog::level::trace;
    config.enable_file_sink = true;
#endif
    return config;
}

}  // namespace

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);

    const AppPaths paths = AppPaths::detect(std::filesystem::path(
        qtrans::app::to_utf8(QCoreApplication::applicationDirPath())));
    paths.ensureDirectories();
    qtrans::log::init(make_log_config(paths));

    if (!SingleInstance::ensurePrimaryOrActivateExisting()) {
        qtrans::log::shutdown();
        return 0;
    }

    QThread worker_thread;
    auto *worker_context = new QObject;
    worker_thread.start();
    worker_context->moveToThread(&worker_thread);

    InferenceService *inference_service = nullptr;
    DownloadService *download_service = nullptr;
    BatchController *batch_controller = nullptr;
    QMetaObject::invokeMethod(worker_context, [&] {
        inference_service = new InferenceService;
        download_service = new DownloadService;
        batch_controller = new BatchController(
            inference_service,
            paths.batch_queue_file,
            paths.batch_output_dir); }, Qt::BlockingQueuedConnection);

    MainWindow window(inference_service, download_service, batch_controller,
                      &worker_thread, paths);
    QObject::connect(&app, &QGuiApplication::applicationStateChanged, &window,
                     [&window](Qt::ApplicationState state) {
                         if (state == Qt::ApplicationActive && !window.isVisible()) {
                             window.bringToForeground();
                         }
                     });
    window.show();

    const int result = app.exec();

    // Shut down the services on their owning worker thread before quitting.
    QMetaObject::invokeMethod(download_service, &DownloadService::shutdown,
                              Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(inference_service, &InferenceService::shutdown,
                              Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(worker_context, [&] {
        delete batch_controller;
        batch_controller = nullptr;
        delete inference_service;
        inference_service = nullptr;
        delete download_service;
        download_service = nullptr;
        worker_context->moveToThread(QCoreApplication::instance()->thread()); }, Qt::BlockingQueuedConnection);
    worker_thread.quit();
    worker_thread.wait();
    delete worker_context;
    qtrans::log::shutdown();

    return result;
}
