#include "ui/single_instance.h"
#include "ui/string_bridge.h"
#include "ui/task_service.h"
#include "ui/mainwindow.h"
#include "log/config.h"
#include "log/init.h"
#include "storage/app_paths.h"

#include <QApplication>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QThread>

#include <filesystem>
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
    TaskService task_service;
    task_service.moveToThread(&worker_thread);
    worker_thread.start();

    MainWindow window(&task_service, &worker_thread, paths);
    QObject::connect(&app, &QGuiApplication::applicationStateChanged, &window,
                     [&window](Qt::ApplicationState state) {
                         if (state == Qt::ApplicationActive && !window.isVisible()) {
                             window.bringToForeground();
                         }
                     });
    window.show();

    const int result = app.exec();

    worker_thread.quit();
    worker_thread.wait();
    qtrans::log::shutdown();

    return result;
}
