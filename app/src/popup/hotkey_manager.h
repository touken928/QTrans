#pragma once

#include <memory>

#include <QObject>
#include <QAbstractNativeEventFilter>
#include <QKeySequence>

class HotkeyManager : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT

public:
    explicit HotkeyManager(QObject *parent = nullptr);
    ~HotkeyManager() override;

    bool registerHotkey(int id, Qt::KeyboardModifiers modifiers, Qt::Key key);
    void unregisterHotkey(int id);
    void unregisterAll();
    bool isRegistered(int id) const;

    bool nativeEventFilter(const QByteArray &eventType, void *message,
                           qintptr *result) override;

signals:
    void hotkeyTriggered(int id);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
