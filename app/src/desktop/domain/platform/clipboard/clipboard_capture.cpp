#include "domain/platform/clipboard/clipboard_capture.h"

#include "domain/logging/component.h"
#include "domain/logging/logger.h"

#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QTimer>
#include <memory>
#include <utility>
#include <vector>
#include <chrono>
#include <thread>

namespace ClipboardCapture {

namespace {

auto clipboard_logger() {
    return qtrans::log::get(qtrans::log::Component::Clipboard);
}

struct MimeSnapshot {
    std::vector<std::pair<QString, QByteArray>> formats;
};

MimeSnapshot snapshot_mime(const QMimeData *mime) {
    MimeSnapshot snapshot;
    if (!mime) return snapshot;
    for (const QString &format : mime->formats()) {
        snapshot.formats.emplace_back(format, mime->data(format));
    }
    return snapshot;
}

bool mime_matches(const QMimeData *mime, const MimeSnapshot &snapshot) {
    if (!mime || mime->formats().size() != static_cast<int>(snapshot.formats.size())) {
        return snapshot.formats.empty() && (!mime || mime->formats().empty());
    }
    for (const auto &[format, data] : snapshot.formats) {
        if (!mime->hasFormat(format) || mime->data(format) != data) return false;
    }
    return true;
}

void restore_mime(QClipboard *clipboard, const MimeSnapshot &snapshot) {
    if (snapshot.formats.empty()) {
        clipboard->clear();
        return;
    }
    auto *mime = new QMimeData;
    for (const auto &[format, data] : snapshot.formats) {
        mime->setData(format, data);
    }
    clipboard->setMimeData(mime);
}

}  // namespace

QString captureSelectedText(int timeoutMs) {
    QClipboard *clipboard = QApplication::clipboard();
    if (!clipboard) {
        return {};
    }

    const MimeSnapshot original = snapshot_mime(clipboard->mimeData());
    clipboard_logger()->trace("captured {} original clipboard MIME formats",
                              original.formats.size());

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
            clipboard_logger()->trace("captured selected text after {} polls ({} characters)",
                                      pollCount, captured.size());
            break;
        }
    }

    if (captured.isEmpty()) {
        clipboard_logger()->warn("timeout after {} polls, no text captured", pollCount);
    }

    const MimeSnapshot copied = snapshot_mime(clipboard->mimeData());
    QTimer::singleShot(200, qApp, [original, copied]() {
        QClipboard *cb = QApplication::clipboard();
        // Restore only if the clipboard still contains the synthetic copy.
        // A real copy made by the user during the delay always wins.
        if (cb && mime_matches(cb->mimeData(), copied)) {
            restore_mime(cb, original);
        }
    });

    if (captured.isEmpty() && mime_matches(clipboard->mimeData(), copied)) {
        restore_mime(clipboard, original);
    }

    return captured.trimmed();
}

}  // namespace ClipboardCapture
