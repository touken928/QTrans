#pragma once

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;

class WordSelectPage : public QWidget {
    Q_OBJECT

public:
    explicit WordSelectPage(QWidget *parent = nullptr);

    void setEnabled(bool enabled);
    void setCloseToTray(bool close_to_tray);
    void setTargetLanguage(const QString &model_name);
    void setHotkey(const QString &shortcut);
    void setAutoCloseMs(int ms);

    bool isEnabled() const;
    bool isCloseToTray() const;
    QString targetLanguage() const;
    QString hotkey() const;
    int autoCloseMs() const;

signals:
    void settingsChanged();

private:
    QCheckBox *enabled_checkbox_ = nullptr;
    QCheckBox *close_to_tray_checkbox_ = nullptr;
    QComboBox *target_lang_combo_ = nullptr;
    QLineEdit *hotkey_edit_ = nullptr;
    QSpinBox *auto_close_spin_ = nullptr;
};
