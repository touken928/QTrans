#pragma once

#include <QObject>
#include <QAbstractNativeEventFilter>
#include <QKeySequence>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <Carbon/Carbon.h>
#endif

class HotkeyManager : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT

public:
    explicit HotkeyManager(QObject* parent = nullptr);
    ~HotkeyManager() override;

    bool registerHotkey(int id, Qt::KeyboardModifiers modifiers, Qt::Key key);
    void unregisterHotkey(int id);
    void unregisterAll();
    bool isRegistered(int id) const;

    bool nativeEventFilter(const QByteArray& eventType, void* message,
                           qintptr* result) override;

signals:
    void hotkeyTriggered(int id);

private:
#ifdef Q_OS_WIN
    struct HotkeyBinding {
        int id = 0;
        UINT nativeMod = 0;
        UINT nativeKey = 0;
    };
    QList<HotkeyBinding> m_bindings;

    static UINT nativeModifiers(Qt::KeyboardModifiers modifiers);
    static UINT nativeKey(Qt::Key key);
#else
    struct HotkeyBinding {
        int id = 0;
        UInt32 carbonMod = 0;
        UInt32 carbonKey = 0;
        EventHotKeyRef ref = nullptr;
    };
    QList<HotkeyBinding> m_bindings;

    static UInt32 carbonModifiers(Qt::KeyboardModifiers modifiers);
    static UInt32 carbonKey(Qt::Key key);
#endif
};
