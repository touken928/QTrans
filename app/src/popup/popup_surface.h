#pragma once

// Neutral popup surface for the word-select popup: cross-platform window
// configuration, front-app handling, and the global Escape monitor installed
// while the popup is visible. The OS-specific implementations live in
// popup/mac/popup_surface.mm and popup/win/popup_surface.cpp. UI code
// includes only this header.

class QObject;

// Presents the popup as a non-activating surface on the active Space/desktop.
void configurePopupWindow(void *native_view);

void saveFrontApp();
void restoreFrontApp();
void reassertFrontApp();
bool ensureAccessibilityTrusted(bool prompt);

// Global Escape swallow while a popup is visible.
// on_escape is invoked on the Qt thread via QueuedConnection, never by
// mutating Qt widgets inside the hook/tap callback.
// modal_consumes_escape may be null. When non-null it is queried from the
// hook/tap the same way today's modalConsumesEscape() is.
void installPopupEscapeMonitor(QObject *owner,
                               void (*on_escape)(QObject *owner),
                               bool (*modal_consumes_escape)());
void uninstallPopupEscapeMonitor(QObject *owner);
