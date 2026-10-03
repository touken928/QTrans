#pragma once

#include <QMetaType>
#include <QString>

// Typed UI projection of one durable batch queue entry. BatchController
// builds it on the worker thread and the batch table model consumes it on the
// UI thread; the value crosses the thread boundary through queueSnapshot.
struct BatchEntryView {
    QString id;
    QString file;  // filename only (today's "file")
    QString file_path;
    QString source;
    QString target;
    int state = 0;  // BatchEntryState as int
    int segments_done = 0;
    int segments_total = 0;
    bool completed = false;
    bool saved = false;
    QString save_path;
};

Q_DECLARE_METATYPE(BatchEntryView)
