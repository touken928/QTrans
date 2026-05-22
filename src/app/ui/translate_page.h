#pragma once

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSplitter;
class QWidget;

class TranslatePage : public QWidget {
    Q_OBJECT

public:
    explicit TranslatePage(QWidget * parent = nullptr);

    void setBusy(bool busy);
    void setModelLoaded(bool loaded);
    void setStatus(const QString & status);
    void resetTarget();
    void resetBackTranslate();
    void appendTarget(const QString & piece);
    void appendBackTranslate(const QString & piece);
    QString targetText() const;
    void prepareForTranslation(bool back_translate);

signals:
    void translateRequested(
        const QString & source,
        const QString & target_language,
        const QString & source_language,
        bool back_translate);

private slots:
    void onTranslate();
    void onSwap();
    void onClear();
    void onCopyResult();
    void onBackTranslateToggled(bool enabled);

private:
    QString targetLanguageName() const;
    QString sourceLanguageName() const;
    void setBackTranslateVisible(bool visible);
    void updateActions();

    QComboBox * source_lang_combo_ = nullptr;
    QComboBox * target_lang_combo_ = nullptr;
    QPushButton * translate_button_ = nullptr;
    QPushButton * swap_button_ = nullptr;
    QPushButton * clear_button_ = nullptr;
    QPushButton * copy_button_ = nullptr;
    QCheckBox * back_translate_checkbox_ = nullptr;
    QSplitter * splitter_ = nullptr;
    QWidget * back_panel_ = nullptr;
    QPlainTextEdit * source_edit_ = nullptr;
    QPlainTextEdit * target_edit_ = nullptr;
    QPlainTextEdit * back_edit_ = nullptr;
    QLabel * status_label_ = nullptr;

    bool busy_ = false;
    bool model_loaded_ = false;
};
