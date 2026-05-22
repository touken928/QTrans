#include "app/backend.h"
#include "app/mainwindow.h"

#include <QApplication>
#include <QThread>

int main(int argc, char * argv[]) {
    QApplication app(argc, argv);

    QThread worker_thread;
    Backend backend;
    backend.moveToThread(&worker_thread);
    worker_thread.start();

    MainWindow window(&backend, &worker_thread);
    window.show();

    const int result = app.exec();

    worker_thread.quit();
    worker_thread.wait();

    return result;
}
