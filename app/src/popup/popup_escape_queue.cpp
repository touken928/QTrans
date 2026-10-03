#include "popup/popup_escape_queue.h"

#include <QMetaObject>
#include <QObject>

void queuePopupEscape(QObject *owner, void (*on_escape)(QObject *)) {
    if (owner == nullptr || on_escape == nullptr) {
        return;
    }
    QMetaObject::invokeMethod(
        owner, [owner, on_escape] { on_escape(owner); }, Qt::QueuedConnection);
}
