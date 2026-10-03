#include "popup/hotkey_manager.h"

#include "logging/component.h"
#include "logging/logger.h"

#include <windows.h>
#include <QCoreApplication>

struct HotkeyManager::Impl {
    struct HotkeyBinding {
        int id = 0;
        UINT nativeMod = 0;
        UINT nativeKey = 0;
    };
    QList<HotkeyBinding> bindings;
};

namespace {
UINT nativeModifiers(Qt::KeyboardModifiers modifiers);
UINT nativeKey(Qt::Key key);
}  // namespace

HotkeyManager::HotkeyManager(QObject *parent)
    : QObject(parent), impl_(std::make_unique<Impl>()) {
    qApp->installNativeEventFilter(this);
}

HotkeyManager::~HotkeyManager() {
    qApp->removeNativeEventFilter(this);
    unregisterAll();
}

bool HotkeyManager::registerHotkey(int id, Qt::KeyboardModifiers modifiers, Qt::Key key) {
    if (isRegistered(id)) {
        unregisterHotkey(id);
    }

    const UINT mod = nativeModifiers(modifiers) | MOD_NOREPEAT;
    const UINT vk = nativeKey(key);

    if (!::RegisterHotKey(nullptr, id, mod, vk)) {
        qtrans::log::get(qtrans::log::Component::WordSelect)
            ->error(
                "RegisterHotKey failed id={} vk=0x{:x} mod=0x{:x} error={}",
                id,
                vk,
                mod,
                static_cast<unsigned long>(::GetLastError()));
        return false;
    }

    qtrans::log::get(qtrans::log::Component::WordSelect)
        ->debug("registered hotkey id={} vk=0x{:x} mod=0x{:x}", id, vk, mod);

    Impl::HotkeyBinding binding{};
    binding.id = id;
    binding.nativeMod = mod;
    binding.nativeKey = vk;
    impl_->bindings.append(binding);
    return true;
}

void HotkeyManager::unregisterHotkey(int id) {
    for (int i = 0; i < impl_->bindings.size(); ++i) {
        if (impl_->bindings[i].id == id) {
            ::UnregisterHotKey(nullptr, impl_->bindings[i].id);
            impl_->bindings.removeAt(i);
            return;
        }
    }
}

void HotkeyManager::unregisterAll() {
    for (const auto &binding : impl_->bindings) {
        ::UnregisterHotKey(nullptr, binding.id);
    }
    impl_->bindings.clear();
}

bool HotkeyManager::isRegistered(int id) const {
    for (const auto &binding : impl_->bindings) {
        if (binding.id == id) {
            return true;
        }
    }
    return false;
}

bool HotkeyManager::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) {
    Q_UNUSED(result);

    if (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG") {
        return false;
    }

    MSG *msg = static_cast<MSG *>(message);
    if (msg->message == WM_HOTKEY) {
        const int id = static_cast<int>(msg->wParam);
        for (const auto &binding : impl_->bindings) {
            if (binding.id == id) {
                emit hotkeyTriggered(id);
                return true;
            }
        }
    }
    return false;
}

namespace {

UINT nativeModifiers(Qt::KeyboardModifiers modifiers) {
    UINT mod = 0;
    if (modifiers & Qt::AltModifier) {
        mod |= MOD_ALT;
    }
    if (modifiers & Qt::ControlModifier) {
        mod |= MOD_CONTROL;
    }
    if (modifiers & Qt::ShiftModifier) {
        mod |= MOD_SHIFT;
    }
    if (modifiers & Qt::MetaModifier) {
        mod |= MOD_WIN;
    }
    return mod;
}

UINT nativeKey(Qt::Key key) {
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        return static_cast<UINT>(key);
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        return static_cast<UINT>(key);
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) {
        return VK_F1 + static_cast<UINT>(key - Qt::Key_F1);
    }
    switch (key) {
        case Qt::Key_Backspace:
            return VK_BACK;
        case Qt::Key_Tab:
            return VK_TAB;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            return VK_RETURN;
        case Qt::Key_Space:
            return VK_SPACE;
        case Qt::Key_Escape:
            return VK_ESCAPE;
        case Qt::Key_Delete:
            return VK_DELETE;
        case Qt::Key_Insert:
            return VK_INSERT;
        case Qt::Key_Home:
            return VK_HOME;
        case Qt::Key_End:
            return VK_END;
        case Qt::Key_PageUp:
            return VK_PRIOR;
        case Qt::Key_PageDown:
            return VK_NEXT;
        case Qt::Key_Left:
            return VK_LEFT;
        case Qt::Key_Right:
            return VK_RIGHT;
        case Qt::Key_Up:
            return VK_UP;
        case Qt::Key_Down:
            return VK_DOWN;
        case Qt::Key_Print:
            return VK_SNAPSHOT;
        case Qt::Key_Pause:
            return VK_PAUSE;
        case Qt::Key_QuoteLeft:
            return VK_OEM_3;
        case Qt::Key_Minus:
            return VK_OEM_MINUS;
        case Qt::Key_Equal:
            return VK_OEM_PLUS;
        case Qt::Key_BracketLeft:
            return VK_OEM_4;
        case Qt::Key_BracketRight:
            return VK_OEM_6;
        case Qt::Key_Backslash:
            return VK_OEM_5;
        case Qt::Key_Semicolon:
            return VK_OEM_1;
        case Qt::Key_Apostrophe:
            return VK_OEM_7;
        case Qt::Key_Comma:
            return VK_OEM_COMMA;
        case Qt::Key_Period:
            return VK_OEM_PERIOD;
        case Qt::Key_Slash:
            return VK_OEM_2;
        default:
            break;
    }
    return 0;
}

}  // namespace
