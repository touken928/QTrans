#include "instance/single_instance.h"
#include "shared/string_bridge.h"
#include "batch/batch_controller.h"
#include "download/download_service.h"
#include "translate/inference_service.h"
#include "translate/local_api_service.h"
#include "worker_host.h"
#include "ui/shell/mainwindow.h"
#include "logging/config.h"
#include "logging/init.h"
#include "paths/app_paths.h"

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

    WorkerHost *worker_host = nullptr;
    QMetaObject::invokeMethod(worker_context, [&] { worker_host = new WorkerHost(paths.batch_queue_file, paths.batch_output_dir); }, Qt::BlockingQueuedConnection);

    MainWindow window(worker_host->inference(), worker_host->download(),
                      worker_host->batch(),
                      new LocalApiService(worker_host->inference()->apiChat()),
                      &worker_thread, paths);
    QObject::connect(&app, &QGuiApplication::applicationStateChanged, &window,
                     [&window](Qt::ApplicationState state) {
                         if (state == Qt::ApplicationActive && !window.isVisible()) {
                             window.bringToForeground();
                         }
                     });
    window.show();

    const int result = app.exec();

    // The local API service is UI-thread owned: shut it down (cancelling any
    // in-flight requests) before the worker-thread services stop, and delete
    // it on its owning (UI) thread.
    auto *local_api_service = window.localApiService();
    local_api_service->stop();

    // Shut down the services on their owning worker thread before quitting.
    // WorkerHost orders DownloadService::shutdown() (joins the download worker)
    // before InferenceService::shutdown().
    QMetaObject::invokeMethod(worker_host, &WorkerHost::shutdown,
                              Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(worker_context, [&] {
        delete worker_host;
        worker_host = nullptr;
        worker_context->moveToThread(QCoreApplication::instance()->thread()); }, Qt::BlockingQueuedConnection);
    worker_thread.quit();
    worker_thread.wait();
    delete worker_context;
    delete local_api_service;
    qtrans::log::shutdown();

    return result;
}
