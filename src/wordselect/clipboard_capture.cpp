#include "wordselect/clipboard_capture.h"

#include <QApplication>
#include <QClipboard>
#include <QTimer>
#include <chrono>
#include <thread>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

void simulateCtrlC() {
#ifdef Q_OS_WIN
    ::keybd_event(VK_CONTROL, 0, 0, 0);
    ::keybd_event('C', 0, 0, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    ::keybd_event('C', 0, KEYEVENTF_KEYUP, 0);
    ::keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
#endif
}

} // namespace

namespace ClipboardCapture {

QString captureSelectedText(int timeoutMs) {
    QClipboard* clipboard = QApplication::clipboard();
    if (!clipboard) {
        return {};
    }

    const QString oldText = clipboard->text();

    clipboard->clear();

    simulateCtrlC();

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

    QString captured;
    constexpr int pollIntervalMs = 30;
    while (std::chrono::steady_clock::now() < deadline) {
        QApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(pollIntervalMs));

        captured = clipboard->text();
        if (!captured.isEmpty()) {
            break;
        }
    }

    QTimer::singleShot(200, qApp, [oldText]() {
        QClipboard* cb = QApplication::clipboard();
        if (cb && !oldText.isEmpty()) {
            cb->setText(oldText);
        }
    });

    if (!oldText.isEmpty() && captured.isEmpty()) {
        clipboard->setText(oldText);
    }

    return captured.trimmed();
}

} // namespace ClipboardCapture
