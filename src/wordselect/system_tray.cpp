#include "wordselect/system_tray.h"

#include <QAction>
#include <QApplication>
#include <QMenu>

SystemTray::SystemTray(QObject* parent)
    : QSystemTrayIcon(parent) {
    setIcon(QIcon(QStringLiteral(":/branding/logo.png")));
    setToolTip(QStringLiteral("QTrans"));

    setupMenu();

    connect(this, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
            emit openMainWindow();
        }
    });

    show();
}

SystemTray::~SystemTray() {
    if (m_menu) {
        m_menu->deleteLater();
        m_menu = nullptr;
    }
}

void SystemTray::setupMenu() {
    m_menu = new QMenu();

    m_openAction = m_menu->addAction(QStringLiteral("Open QTrans"));
    connect(m_openAction, &QAction::triggered, this, &SystemTray::openMainWindow);

    m_menu->addSeparator();

    m_toggleAction = m_menu->addAction(QStringLiteral("Pause Word Select Translation"));
    m_toggleAction->setCheckable(true);
    m_toggleAction->setChecked(false);
    connect(m_toggleAction, &QAction::toggled, this, [this](bool checked) {
        emit toggleTranslation(!checked);
    });

    m_menu->addSeparator();

    m_quitAction = m_menu->addAction(QStringLiteral("Quit"));
    connect(m_quitAction, &QAction::triggered, this, &SystemTray::quitApp);

    setContextMenu(m_menu);
}

void SystemTray::setTranslationEnabled(bool enabled) {
    if (m_toggleAction) {
        m_toggleAction->setChecked(!enabled);
    }
}
