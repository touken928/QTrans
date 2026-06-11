#include "wordselect/clipboard_capture.h"

#include "ui/string_bridge.h"
#include "log/component.h"
#include "log/logger.h"

#include <QApplication>
#include <QClipboard>
#include <QTimer>
#include <chrono>
#include <thread>

namespace ClipboardCapture {

namespace {

auto clipboard_logger() {
    return qtrans::log::get(qtrans::log::Component::Clipboard);
}

}  // namespace

QString captureSelectedText(int timeoutMs) {
    QClipboard *clipboard = QApplication::clipboard();
    if (!clipboard) {
        return {};
    }

    const QString oldText = clipboard->text();
    clipboard_logger()->trace(
        "oldText '{}' (empty={})",
        qtrans::app::to_utf8(oldText),
        oldText.isEmpty());

    clipboard->clear();
    clipboard_logger()->debug("clipboard cleared, simulating pasteboard copy");

    simulateCopy();
    clipboard_logger()->debug("copy simulated, polling clipboard");

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
            clipboard_logger()->trace(
                "captured after {} polls: '{}'",
                pollCount,
                qtrans::app::to_utf8(captured));
            break;
        }
    }

    if (captured.isEmpty()) {
        clipboard_logger()->warn("timeout after {} polls, no text captured", pollCount);
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
