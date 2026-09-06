#include "ui/pages/batch/batch_table_view.h"

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QUrl>

bool BatchTableView::hasLocalFileDrag(const QMimeData *mime) {
    if (mime == nullptr || !mime->hasUrls()) {
        return false;
    }
    const QList<QUrl> urls = mime->urls();
    if (urls.isEmpty()) {
        return false;
    }
    for (const QUrl &url : urls) {
        if (!url.isLocalFile()) {
            return false;
        }
    }
    return true;
}

QStringList BatchTableView::localFilePaths(const QMimeData *mime) {
    QStringList paths;
    if (mime == nullptr || !mime->hasUrls()) {
        return paths;
    }
    const QList<QUrl> urls = mime->urls();
    paths.reserve(urls.size());
    for (const QUrl &url : urls) {
        if (url.isLocalFile()) {
            paths.append(url.toLocalFile());
        }
    }
    return paths;
}

BatchTableView::BatchTableView(QWidget *parent)
    : QTableView(parent) {
    setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::DropOnly);
}

void BatchTableView::dragEnterEvent(QDragEnterEvent *event) {
    if (hasLocalFileDrag(event->mimeData())) {
        event->acceptProposedAction();
        return;
    }
    QTableView::dragEnterEvent(event);
}

void BatchTableView::dragMoveEvent(QDragMoveEvent *event) {
    if (hasLocalFileDrag(event->mimeData())) {
        event->acceptProposedAction();
        return;
    }
    QTableView::dragMoveEvent(event);
}

void BatchTableView::dropEvent(QDropEvent *event) {
    if (!hasLocalFileDrag(event->mimeData())) {
        QTableView::dropEvent(event);
        return;
    }
    const QStringList paths = localFilePaths(event->mimeData());
    if (!paths.isEmpty()) {
        emit filesDropped(paths);
        event->acceptProposedAction();
    }
}

void BatchTableView::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Delete &&
        selectionModel() != nullptr && selectionModel()->hasSelection()) {
        emit removeSelectionRequested();
        event->accept();
        return;
    }
    QTableView::keyPressEvent(event);
}
