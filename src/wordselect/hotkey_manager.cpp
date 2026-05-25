#include "wordselect/hotkey_manager.h"

#include <QCoreApplication>

#ifdef Q_OS_WIN
// Windows implementation using RegisterHotKey

HotkeyManager::HotkeyManager(QObject* parent)
    : QObject(parent) {
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
        return false;
    }

    HotkeyBinding binding{};
    binding.id = id;
    binding.nativeMod = mod;
    binding.nativeKey = vk;
    m_bindings.append(binding);
    return true;
}

void HotkeyManager::unregisterHotkey(int id) {
    for (int i = 0; i < m_bindings.size(); ++i) {
        if (m_bindings[i].id == id) {
            ::UnregisterHotKey(nullptr, m_bindings[i].id);
            m_bindings.removeAt(i);
            return;
        }
    }
}

void HotkeyManager::unregisterAll() {
    for (const auto& binding : m_bindings) {
        ::UnregisterHotKey(nullptr, binding.id);
    }
    m_bindings.clear();
}

bool HotkeyManager::isRegistered(int id) const {
    for (const auto& binding : m_bindings) {
        if (binding.id == id) {
            return true;
        }
    }
    return false;
}

bool HotkeyManager::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) {
    Q_UNUSED(result);

    if (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG") {
        return false;
    }

    MSG* msg = static_cast<MSG*>(message);
    if (msg->message == WM_HOTKEY) {
        const int id = static_cast<int>(msg->wParam);
        for (const auto& binding : m_bindings) {
            if (binding.id == id) {
                emit hotkeyTriggered(id);
                return true;
            }
        }
    }
    return false;
}

UINT HotkeyManager::nativeModifiers(Qt::KeyboardModifiers modifiers) {
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

UINT HotkeyManager::nativeKey(Qt::Key key) {
    // Letters A-Z
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        return static_cast<UINT>(key);
    }
    // Numbers 0-9
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        return static_cast<UINT>(key);
    }
    // Function keys
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) {
        return VK_F1 + static_cast<UINT>(key - Qt::Key_F1);
    }
    // Special keys
    switch (key) {
    case Qt::Key_Backspace:  return VK_BACK;
    case Qt::Key_Tab:        return VK_TAB;
    case Qt::Key_Return:
    case Qt::Key_Enter:      return VK_RETURN;
    case Qt::Key_Space:      return VK_SPACE;
    case Qt::Key_Escape:     return VK_ESCAPE;
    case Qt::Key_Delete:     return VK_DELETE;
    case Qt::Key_Insert:     return VK_INSERT;
    case Qt::Key_Home:       return VK_HOME;
    case Qt::Key_End:        return VK_END;
    case Qt::Key_PageUp:     return VK_PRIOR;
    case Qt::Key_PageDown:   return VK_NEXT;
    case Qt::Key_Left:       return VK_LEFT;
    case Qt::Key_Right:      return VK_RIGHT;
    case Qt::Key_Up:         return VK_UP;
    case Qt::Key_Down:       return VK_DOWN;
    case Qt::Key_Print:      return VK_SNAPSHOT;
    case Qt::Key_Pause:      return VK_PAUSE;
    case Qt::Key_QuoteLeft:  return VK_OEM_3;     // backtick/tilde
    case Qt::Key_Minus:      return VK_OEM_MINUS;
    case Qt::Key_Equal:      return VK_OEM_PLUS;
    case Qt::Key_BracketLeft:  return VK_OEM_4;
    case Qt::Key_BracketRight: return VK_OEM_6;
    case Qt::Key_Backslash:  return VK_OEM_5;
    case Qt::Key_Semicolon:  return VK_OEM_1;
    case Qt::Key_Apostrophe: return VK_OEM_7;
    case Qt::Key_Comma:      return VK_OEM_COMMA;
    case Qt::Key_Period:     return VK_OEM_PERIOD;
    case Qt::Key_Slash:      return VK_OEM_2;
    default: break;
    }
    return 0;
}

#else // macOS implementation using Carbon Event HotKeys

#include <Carbon/Carbon.h>

namespace {
EventHandlerUPP gHotkeyHandlerUPP = nullptr;

OSStatus hotkeyHandler(EventHandlerCallRef /*next*/, EventRef event, void* userData) {
    auto* mgr = static_cast<HotkeyManager*>(userData);
    if (!mgr) return eventNotHandledErr;

    EventHotKeyID hotkeyId;
    if (GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID,
                          nullptr, sizeof(hotkeyId), nullptr, &hotkeyId) == noErr) {
        emit mgr->hotkeyTriggered(static_cast<int>(hotkeyId.id));
        return noErr;
    }
    return eventNotHandledErr;
}
}

HotkeyManager::HotkeyManager(QObject* parent)
    : QObject(parent) {
    qApp->installNativeEventFilter(this);

    EventTypeSpec eventTypes[] = {
        {kEventClassKeyboard, kEventHotKeyPressed}
    };
    gHotkeyHandlerUPP = NewEventHandlerUPP(hotkeyHandler);
    InstallApplicationEventHandler(gHotkeyHandlerUPP, 1, eventTypes, this, nullptr);
}

HotkeyManager::~HotkeyManager() {
    qApp->removeNativeEventFilter(this);
    unregisterAll();
    if (gHotkeyHandlerUPP) {
        RemoveEventHandler(gHotkeyHandlerUPP);
        DisposeEventHandlerUPP(gHotkeyHandlerUPP);
        gHotkeyHandlerUPP = nullptr;
    }
}

bool HotkeyManager::registerHotkey(int id, Qt::KeyboardModifiers modifiers, Qt::Key key) {
    if (isRegistered(id)) {
        unregisterHotkey(id);
    }

    const UInt32 mod = carbonModifiers(modifiers);
    const UInt32 vk = carbonKey(key);

    EventHotKeyID hotkeyId{};
    hotkeyId.signature = FOUR_CHAR_CODE('QTrs');
    hotkeyId.id = static_cast<UInt32>(id);

    EventHotKeyRef ref = nullptr;
    const OSStatus status = RegisterEventHotKey(vk, mod, hotkeyId,
                                                 GetApplicationEventTarget(), 0, &ref);
    if (status != noErr) {
        return false;
    }

    HotkeyBinding binding{};
    binding.id = id;
    binding.carbonMod = mod;
    binding.carbonKey = vk;
    binding.ref = ref;
    m_bindings.append(binding);
    return true;
}

void HotkeyManager::unregisterHotkey(int id) {
    for (int i = 0; i < m_bindings.size(); ++i) {
        if (m_bindings[i].id == id) {
            UnregisterEventHotKey(m_bindings[i].ref);
            m_bindings.removeAt(i);
            return;
        }
    }
}

void HotkeyManager::unregisterAll() {
    for (const auto& binding : m_bindings) {
        UnregisterEventHotKey(binding.ref);
    }
    m_bindings.clear();
}

bool HotkeyManager::isRegistered(int id) const {
    for (const auto& binding : m_bindings) {
        if (binding.id == id) {
            return true;
        }
    }
    return false;
}

bool HotkeyManager::nativeEventFilter(const QByteArray& eventType, void* /*message*/, qintptr* /*result*/) {
    Q_UNUSED(eventType);
    return false;
}

UInt32 HotkeyManager::carbonModifiers(Qt::KeyboardModifiers modifiers) {
    UInt32 mod = 0;
    if (modifiers & Qt::ControlModifier) {
        mod |= cmdKey;
    }
    if (modifiers & Qt::ShiftModifier) {
        mod |= shiftKey;
    }
    if (modifiers & Qt::AltModifier) {
        mod |= optionKey;
    }
    if (modifiers & Qt::MetaModifier) {
        mod |= controlKey;
    }
    return mod;
}

UInt32 HotkeyManager::carbonKey(Qt::Key key) {
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        return static_cast<UInt32>(key);
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        return static_cast<UInt32>(key);
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F12) {
        return kVK_F1 + static_cast<UInt32>(key - Qt::Key_F1);
    }
    switch (key) {
    case Qt::Key_Return:
    case Qt::Key_Enter:      return kVK_Return;
    case Qt::Key_Tab:        return kVK_Tab;
    case Qt::Key_Space:      return kVK_Space;
    case Qt::Key_Backspace:  return kVK_Delete;
    case Qt::Key_Escape:     return kVK_Escape;
    case Qt::Key_Delete:     return kVK_ForwardDelete;
    case Qt::Key_Home:       return kVK_Home;
    case Qt::Key_End:        return kVK_End;
    case Qt::Key_PageUp:     return kVK_PageUp;
    case Qt::Key_PageDown:   return kVK_PageDown;
    case Qt::Key_Left:       return kVK_LeftArrow;
    case Qt::Key_Right:      return kVK_RightArrow;
    case Qt::Key_Up:         return kVK_UpArrow;
    case Qt::Key_Down:       return kVK_DownArrow;
    case Qt::Key_QuoteLeft:  return kVK_ANSI_Grave;
    case Qt::Key_Minus:      return kVK_ANSI_Minus;
    case Qt::Key_Equal:      return kVK_ANSI_Equal;
    case Qt::Key_BracketLeft:  return kVK_ANSI_LeftBracket;
    case Qt::Key_BracketRight: return kVK_ANSI_RightBracket;
    case Qt::Key_Backslash:  return kVK_ANSI_Backslash;
    case Qt::Key_Semicolon:  return kVK_ANSI_Semicolon;
    case Qt::Key_Apostrophe: return kVK_ANSI_Quote;
    case Qt::Key_Comma:      return kVK_ANSI_Comma;
    case Qt::Key_Period:     return kVK_ANSI_Period;
    case Qt::Key_Slash:      return kVK_ANSI_Slash;
    default: break;
    }
    return 0;
}

#endif // Q_OS_WIN / macOS
