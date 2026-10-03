#pragma once

#include "domain/inference/inference_types.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSplitter;

// Professional translation workbench. A compact toolbar holds the language
// selectors, swap, and the primary action; below it a two-pane source/result
// workspace streams the translation. Back-translation is an optional third
// pane that only appears while it is active, so the default layout stays a
// clean two-column workbench.
//
// The workspace is width-responsive: pane minimums are reduced so the
// three-pane (back-translation) layout still fits the narrowest supported
// window (720 px), and when the panes cannot fit side by side the splitter
// stacks vertically. Without back-translation the presentation is always
// the two-pane desktop splitter.
//
// The result pane carries a terminal-state strip (translating / completed /
// cancelled / preempted / failed) so the outcome of a job is always visible
// without any token-internal detail. A failed job keeps its request and
// offers Retry, which resubmits the exact same source/languages/back flag.
class TranslatePage : public QWidget {
    Q_OBJECT

public:
    explicit TranslatePage(QWidget *parent = nullptr);

    void setBusy(bool busy);
    void setTranslating(bool translating);
    void setModelLoaded(bool loaded);
    // Terminal outcome of the last submitted job, rendered in the result
    // pane's state strip. Only Failed/Cancelled/Preempted/Completed reach
    // this; the strip stays on "translating" while a job runs.
    void setTranslationResult(TranslationState state, const QString &error_message = {});
    void resetTarget();
    void resetBackTranslate();
    void appendTarget(const QString &piece);
    void appendBackTranslate(const QString &piece);
    QString targetText() const;
    void prepareForTranslation(bool back_translate);

    void setSourceLanguage(const QString &model_name);
    void setTargetLanguage(const QString &model_name);
    QString targetLanguageName() const;
    QString sourceLanguageName() const;

signals:
    void translateRequested(
        const QString &source,
        const QString &target_language,
        const QString &source_language,
        bool back_translate);
    void cancelRequested();
    void languageChanged();

protected:
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onTranslate();
    void onClear();
    void onCopyResult();
    void onCopyBackResult();
    void onBackTranslateToggled(bool enabled);
    void onRetry();

private:
    void setBackTranslateVisible(bool visible);
    void updateActions();
    void updateCharCounts();
    void updateResultStrip();
    bool shouldStackPanes() const;
    void updateSplitterOrientation();
    // Reconciles the back pane header with its current width: the title
    // elides into the space the fixed Copy button leaves, and the character
    // count (secondary metadata) hides before the header can overflow.
    void updateBackHeader();

    QComboBox *source_lang_combo_ = nullptr;
    QComboBox *target_lang_combo_ = nullptr;
    QPushButton *translate_button_ = nullptr;
    QPushButton *clear_button_ = nullptr;
    QPushButton *copy_button_ = nullptr;
    QPushButton *back_copy_button_ = nullptr;
    QCheckBox *back_translate_checkbox_ = nullptr;
    QSplitter *splitter_ = nullptr;
    QWidget *back_panel_ = nullptr;
    QPlainTextEdit *source_edit_ = nullptr;
    QPlainTextEdit *target_edit_ = nullptr;
    QPlainTextEdit *back_edit_ = nullptr;
    QLabel *back_title_ = nullptr;
    QLabel *source_count_label_ = nullptr;
    QLabel *target_count_label_ = nullptr;
    QLabel *back_count_label_ = nullptr;
    QWidget *result_strip_ = nullptr;
    QLabel *result_dot_ = nullptr;
    QLabel *result_state_label_ = nullptr;
    QPushButton *retry_button_ = nullptr;

    bool busy_ = false;
    bool translating_ = false;
    bool model_loaded_ = false;
    // True while the last finished job failed, so Retry stays available
    // until the next submission.
    bool last_failed_ = false;

    // Retained request for the Retry affordance.
    QString last_source_;
    QString last_target_language_;
    QString last_source_language_;
    bool last_back_translate_ = false;
};
