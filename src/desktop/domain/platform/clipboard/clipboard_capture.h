#pragma once

#include <QString>

namespace ClipboardCapture {

void simulateCopy();
QString captureSelectedText(int timeoutMs = 300);

}  // namespace ClipboardCapture
