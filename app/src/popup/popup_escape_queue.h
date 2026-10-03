#pragma once

class QObject;

// Queues an Escape dismissal onto the Qt thread. Kept in a .cpp translation
// unit because QMetaObject::invokeMethod's functor overload does not link
// from an Objective-C++ file.
void queuePopupEscape(QObject *owner, void (*on_escape)(QObject *owner));
