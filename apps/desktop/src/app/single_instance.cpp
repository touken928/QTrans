#include "app/single_instance.h"

#include <QDir>
#include <QLockFile>

#if defined(Q_OS_WIN)
void activateExistingApplication();
#elif defined(Q_OS_MACOS)
void activateExistingApplication();
#endif

bool SingleInstance::ensurePrimaryOrActivateExisting() {
    static QLockFile lock_file(QDir::temp().absoluteFilePath(QStringLiteral("QTrans.lock")));
    lock_file.setStaleLockTime(3000);

    if (!lock_file.tryLock(100)) {
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
        activateExistingApplication();
#endif
        return false;
    }

    return true;
}
