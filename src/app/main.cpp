#include "app/task_service.h"
#include "app/mainwindow.h"

#include <QApplication>
#include <QThread>

int main(int argc, char * argv[]) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);

    QThread worker_thread;
    TaskService task_service;
    task_service.moveToThread(&worker_thread);
    worker_thread.start();

    MainWindow window(&task_service, &worker_thread);
    window.show();

    const int result = app.exec();

    worker_thread.quit();
    worker_thread.wait();

    return result;
}
