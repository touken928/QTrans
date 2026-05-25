#pragma once

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;

class WordSelectPage : public QWidget {
    Q_OBJECT

public:
    explicit WordSelectPage(QWidget * parent = nullptr);

    void setEnabled(bool enabled);
    void setSourceLanguage(const QString & model_name);
    void setTargetLanguage(const QString & model_name);
    void setHotkey(const QString & shortcut);
    void setAutoCloseMs(int ms);

    bool isEnabled() const;
    QString sourceLanguage() const;
    QString targetLanguage() const;
    QString hotkey() const;
    int autoCloseMs() const;

signals:
    void settingsChanged();

private:
    QCheckBox * enabled_checkbox_ = nullptr;
    QComboBox * source_lang_combo_ = nullptr;
    QComboBox * target_lang_combo_ = nullptr;
    QLineEdit * hotkey_edit_ = nullptr;
    QSpinBox * auto_close_spin_ = nullptr;
};
