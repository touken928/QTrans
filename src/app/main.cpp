#include "app/single_instance.h"
#include "app/task_service.h"
#include "app/mainwindow.h"

#include <QApplication>
#include <QGuiApplication>
#include <QThread>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);

    if (!SingleInstance::ensurePrimaryOrActivateExisting()) {
        return 0;
    }

    QThread worker_thread;
    TaskService task_service;
    task_service.moveToThread(&worker_thread);
    worker_thread.start();

    MainWindow window(&task_service, &worker_thread);
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

    return result;
}
