#pragma once

#include <QWidget>

class QCheckBox;
class QComboBox;
class QKeySequenceEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QSpinBox;

// Real Preferences destination: a scrollable page organised into General,
// Word Selection, Integrations (Local API), and Advanced sections. The
// Phase-1 placeholder host is gone — every control lives here directly, and
// the page owns its own (single) padding set, so margins never compound.
//
// The page only reports settings; MainWindow validates, applies, and
// persists them (atomicity lives there). Local feedback for validation and
// registration outcomes is rendered through setFeedback().
class PreferencesPage : public QWidget {
    Q_OBJECT

public:
    explicit PreferencesPage(QWidget *parent = nullptr);

    // ── General ──────────────────────────────────────────────────────
    void setCloseToTray(bool close_to_tray);
    bool isCloseToTray() const;

    // ── Word Selection ────────────────────────────────────────────────
    void setEnabled(bool enabled);
    bool isEnabled() const;
    void setTargetLanguage(const QString &model_name);
    QString targetLanguage() const;
    void setHotkey(const QString &shortcut);
    QString hotkey() const;
    void setAutoCloseMs(int ms);
    int autoCloseMs() const;

    // ── Integrations / Local API ──────────────────────────────────────
    void setApiEnabled(bool enabled);
    bool isApiEnabled() const;
    void setApiPort(int port);
    int apiPort() const;

    // ── Advanced ──────────────────────────────────────────────────────
    void setDataDirectory(const QString &path);

    // Inline feedback under the Word Selection section. `error` picks the
    // red variant; an empty message hides the label content.
    void setFeedback(const QString &message, bool error);

signals:
    void settingsChanged();

private:
    void updateEndpoint();
    void onHotkeyEdited();

    QCheckBox *close_to_tray_checkbox_ = nullptr;
    QCheckBox *enabled_checkbox_ = nullptr;
    QComboBox *target_lang_combo_ = nullptr;
    QKeySequenceEdit *hotkey_edit_ = nullptr;
    QSpinBox *auto_close_spin_ = nullptr;
    QCheckBox *api_checkbox_ = nullptr;
    QSpinBox *api_port_spin_ = nullptr;
    QLineEdit *endpoint_edit_ = nullptr;
    QPushButton *endpoint_copy_button_ = nullptr;
    QLineEdit *data_dir_edit_ = nullptr;
    QLabel *feedback_label_ = nullptr;
};
