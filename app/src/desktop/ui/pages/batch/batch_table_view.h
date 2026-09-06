#pragma once

#include <QStringList>
#include <QTableView>

class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QKeyEvent;
class QMimeData;

// Work-queue table view. Adds drag-and-drop file enqueue (accepts local
// files dropped onto the queue) and Delete-key removal of the selection.
// Everything else — multi-selection, Ctrl+A, arrow navigation, sorting — is
// stock QTableView behaviour so keyboard users and screen readers get the
// standard table semantics.
class BatchTableView : public QTableView {
    Q_OBJECT

public:
    explicit BatchTableView(QWidget *parent = nullptr);

    // True when the payload only carries local file URLs.
    static bool hasLocalFileDrag(const QMimeData *mime);
    // Local file paths from a drag payload, in drop order.
    static QStringList localFilePaths(const QMimeData *mime);

signals:
    // Local file paths dropped onto the queue, in drop order.
    void filesDropped(const QStringList &paths);
    // The Delete key was pressed with a non-empty selection.
    void removeSelectionRequested();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
};
