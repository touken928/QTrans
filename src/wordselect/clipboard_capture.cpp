#include "wordselect/clipboard_capture.h"

#include <QApplication>
#include <QClipboard>
#include <QTimer>
#include <chrono>
#include <cstdio>
#include <thread>

namespace ClipboardCapture {

QString captureSelectedText(int timeoutMs) {
    QClipboard *clipboard = QApplication::clipboard();
    if (!clipboard) {
        return {};
    }

    const QString oldText = clipboard->text();
    fprintf(stderr, "[ClipboardCapture] oldText '%s' (empty=%d)\n",
            oldText.toUtf8().constData(), oldText.isEmpty());

    clipboard->clear();
    fprintf(stderr, "[ClipboardCapture] clipboard cleared, simulating pasteboard copy\n");

    simulateCopy();
    fprintf(stderr, "[ClipboardCapture] copy simulated, polling clipboard\n");

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

    QString captured;
    constexpr int pollIntervalMs = 30;
    int pollCount = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        QApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(pollIntervalMs));

        captured = clipboard->text();
        ++pollCount;
        if (!captured.isEmpty()) {
            fprintf(stderr, "[ClipboardCapture] captured after %d polls: '%s'\n",
                    pollCount, captured.toUtf8().constData());
            break;
        }
    }

    if (captured.isEmpty()) {
        fprintf(stderr, "[ClipboardCapture] timeout after %d polls, no text captured\n", pollCount);
    }

    QTimer::singleShot(200, qApp, [oldText]() {
        QClipboard *cb = QApplication::clipboard();
        if (cb && !oldText.isEmpty()) {
            cb->setText(oldText);
        }
    });

    if (!oldText.isEmpty() && captured.isEmpty()) {
        clipboard->setText(oldText);
    }

    return captured.trimmed();
}

}  // namespace ClipboardCapture
