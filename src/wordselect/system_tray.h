#pragma once

#include <QSystemTrayIcon>

class QMenu;
class QAction;

class SystemTray : public QSystemTrayIcon
{
    Q_OBJECT

public:
    explicit SystemTray(QObject* parent = nullptr);
    ~SystemTray() override;

    void setupMenu();

public slots:
    void setTranslationEnabled(bool enabled);

signals:
    void openMainWindow();
    void toggleTranslation(bool enabled);
    void quitApp();

private:
    QMenu* m_menu = nullptr;
    QAction* m_openAction = nullptr;
    QAction* m_toggleAction = nullptr;
    QAction* m_quitAction = nullptr;
};
