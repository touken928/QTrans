#include "ui/pages/translate/translate_page.h"
#include "ui/shared/theme/theme.h"
#include "shared/string_bridge.h"
#include "ui/shared/widget_utils.h"
#include "domain/logging/component.h"
#include "domain/logging/logger.h"
#include "domain/model-catalog/language_list.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSpacerItem>
#include <QSplitter>
#include <QStyle>
#include <QTextCursor>
#include <QVBoxLayout>

namespace {

// ── Pane geometry ─────────────────────────────────────────────────────
// The workspace must work at the narrowest supported window (720 px wide:
// 72 px nav rail + 48 px page margins leave ~600 px for the splitter).
// Panes use a reduced hard floor (160 px each), and when the back-translate
// pane makes three panes that would fall below ~200 px each, the splitter
// stacks vertically so panes never overlap, clip, or get squeezed into
// unusable columns. Without back-translation the presentation is always the
// two-pane horizontal splitter.
constexpr int kPaneMinWidth = 160;
constexpr int kPaneMinHeight = 96;
constexpr int kStackThresholdWidth = 3 * 200 + 2;  // ~200 px per pane + handles

int defaultLanguageIndex(const char *id) {
    for (int i = 0; i < translation_language_count(); ++i) {
        if (qtrans::app::from_utf8(translation_languages()[i].id) == qtrans::app::from_utf8(id)) {
            return i;
        }
    }
    return 0;
}

QString languageNameAt(QComboBox *combo) {
    const int index = combo->currentIndex();
    if (index < 0 || index >= translation_language_count()) {
        return QStringLiteral("English");
    }
    return qtrans::app::from_utf8(translation_languages()[index].model_name);
}

QString charCountText(const QString &text) {
    return QStringLiteral("%1 characters").arg(text.size());
}

// Refreshes a dynamic-property selector (e.g. [stripState="failed"]) after
// the property changed, so the stylesheet retints the widget immediately.
void repolish(QWidget *widget) {
    if (widget != nullptr && widget->style() != nullptr) {
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
    }
}

QString stripStateFor(TranslationState state) {
    switch (state) {
        case TranslationState::Completed:
            return QStringLiteral("completed");
        case TranslationState::Cancelled:
            return QStringLiteral("cancelled");
        case TranslationState::Preempted:
            return QStringLiteral("preempted");
        case TranslationState::Failed:
            return QStringLiteral("failed");
        case TranslationState::Pending:
        case TranslationState::Running:
            return QStringLiteral("translating");
    }
    return QStringLiteral("translating");
}

QString stripTextFor(TranslationState state, const QString &error_message) {
    switch (state) {
        case TranslationState::Completed:
            return QStringLiteral("Translation complete");
        case TranslationState::Cancelled:
            return QStringLiteral("Cancelled");
        case TranslationState::Preempted:
            return QStringLiteral("Interrupted");
        case TranslationState::Failed:
            return error_message.isEmpty() ? QStringLiteral("Translation failed")
                                           : error_message;
        case TranslationState::Pending:
        case TranslationState::Running:
            return QStringLiteral("Translating\u2026");
    }
    return QStringLiteral("Translating\u2026");
}

}  // namespace

TranslatePage::TranslatePage(QWidget *parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("page"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(Theme::Space::xxl, Theme::Space::xxl,
                             Theme::Space::xxl, Theme::Space::xxl);
    root->setSpacing(Theme::Space::lg);

    // ── Language toolbar card (compact operational row) ─────────────────
    // One clear control row in the style of the Documents page: direct
    // From/To labels around the language combos with a direction arrow, and
    // the primary Translate action on the right. The Back translate option
    // sits immediately left of the primary action: physically adjacent so
    // the workbench option and its action read as one unit, yet visually
    // distinct (a plain checkbox next to the teal primary button).
    auto *toolbar = new QWidget(this);
    toolbar->setObjectName(QStringLiteral("translateToolbar"));
    // Stable, comfortably tall control row: the From/To combos and the
    // Translate button must never look vertically squeezed at any window
    // height (sized from the theme, not spacers).
    toolbar->setMinimumHeight(Theme::Size::toolbarHeight);
    auto *toolbar_layout = new QHBoxLayout(toolbar);
    // The horizontal inset MUST live in the layout margins: QSS padding on
    // a plain container QWidget does not affect layout geometry in Qt
    // (contentsRect() ignores it), so a stylesheet padding is silently
    // neutralized. These explicit margins match the Documents page language
    // bar exactly and cannot be overridden by any stylesheet.
    toolbar_layout->setContentsMargins(Theme::Space::lg, Theme::Space::sm,
                                       Theme::Space::lg, Theme::Space::sm);
    toolbar_layout->setSpacing(Theme::Space::sm);

    auto *from_caption = new QLabel(QStringLiteral("From"), toolbar);
    from_caption->setObjectName(QStringLiteral("toolbarCaption"));
    from_caption->setAccessibleName(QStringLiteral("Source language"));
    toolbar_layout->addWidget(from_caption);

    source_lang_combo_ = new QComboBox(toolbar);
    for (int i = 0; i < translation_language_count(); ++i) {
        source_lang_combo_->addItem(qtrans::app::from_utf8(translation_languages()[i].label));
    }
    source_lang_combo_->setCurrentIndex(defaultLanguageIndex("en"));
    source_lang_combo_->setToolTip(QStringLiteral("Source language of the text to translate"));
    source_lang_combo_->setAccessibleName(QStringLiteral("Source language"));
    configureComboBox(source_lang_combo_, 100);
    toolbar_layout->addWidget(source_lang_combo_);

    auto *arrow_label = new QLabel(QStringLiteral("\u2192"), toolbar);
    arrow_label->setObjectName(QStringLiteral("toolbarCaption"));
    arrow_label->setAccessibleName(QStringLiteral("to"));
    toolbar_layout->addWidget(arrow_label);

    auto *to_caption = new QLabel(QStringLiteral("To"), toolbar);
    to_caption->setObjectName(QStringLiteral("toolbarCaption"));
    to_caption->setAccessibleName(QStringLiteral("Target language"));
    toolbar_layout->addWidget(to_caption);

    target_lang_combo_ = new QComboBox(toolbar);
    for (int i = 0; i < translation_language_count(); ++i) {
        target_lang_combo_->addItem(qtrans::app::from_utf8(translation_languages()[i].label));
    }
    target_lang_combo_->setCurrentIndex(defaultLanguageIndex("zh"));
    target_lang_combo_->setToolTip(QStringLiteral("Language to translate into"));
    target_lang_combo_->setAccessibleName(QStringLiteral("Target language"));
    configureComboBox(target_lang_combo_, 100);
    toolbar_layout->addWidget(target_lang_combo_);

    toolbar_layout->addStretch(1);

    back_translate_checkbox_ = new QCheckBox(QStringLiteral("Back translate"),
                                             toolbar);
    // The label must always render fully: the default Preferred horizontal
    // policy lets the row compress the checkbox below its label width when
    // space is tight, truncating the text right at the button's left edge
    // (read as the button covering it). A Fixed policy puts the label's full
    // sizeHint into the layout minimum, so the checkbox can never be
    // squeezed and the Translate button can never invade its rect.
    back_translate_checkbox_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    back_translate_checkbox_->setToolTip(
        QStringLiteral("Shows a third pane with the translation re-translated "
                       "into the source language"));
    back_translate_checkbox_->setAccessibleName(
        QStringLiteral("Back translate"));
    back_translate_checkbox_->setAccessibleDescription(
        QStringLiteral("Translate the result back into the source language in a third pane"));
    toolbar_layout->addWidget(back_translate_checkbox_);

    // Guaranteed clearance between the checkbox label and the Translate
    // button: at the minimum window the row is over-constrained (the QSS-
    // styled combos carry 163px sizeHints), and QHBoxLayout collapses item
    // spacing toward zero under deficit — the button would land flush
    // against the checkbox and paint its border over the label's last
    // pixels. A Fixed spacer cannot shrink, so the label always keeps a
    // real gap from the button on every platform/style.
    toolbar_layout->addSpacerItem(new QSpacerItem(Theme::Space::md, 0,
                                                  QSizePolicy::Fixed,
                                                  QSizePolicy::Minimum));

    translate_button_ = new QPushButton(QStringLiteral("Translate"), toolbar);
    translate_button_->setObjectName(QStringLiteral("translateButton"));
    translate_button_->setCursor(Qt::PointingHandCursor);
    translate_button_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Return")));
#ifdef Q_OS_MACOS
    translate_button_->setToolTip(QStringLiteral("Translate (Cmd+Return)"));
#else
    translate_button_->setToolTip(QStringLiteral("Translate (Ctrl+Return)"));
#endif
    {
        QPushButton probe(QStringLiteral("Stop"), toolbar);
        probe.setObjectName(translate_button_->objectName());
        probe.setFont(translate_button_->font());
        const int w = qMax(translate_button_->sizeHint().width(),
                           probe.sizeHint().width()) +
                      16;
        translate_button_->setFixedWidth(w);
    }
    toolbar_layout->addWidget(translate_button_);

    root->addWidget(toolbar);

    // ── Source / result / back-translate workspace ─────────────────────
    splitter_ = new QSplitter(Qt::Horizontal, this);
    splitter_->setHandleWidth(1);
    splitter_->setChildrenCollapsible(false);

    // Source pane
    auto *source_panel = new QFrame(splitter_);
    source_panel->setObjectName(QStringLiteral("workPane"));
    source_panel->setMinimumWidth(kPaneMinWidth);
    auto *src_layout = new QVBoxLayout(source_panel);
    src_layout->setContentsMargins(Theme::Space::lg, Theme::Space::sm,
                                   Theme::Space::lg, Theme::Space::lg);
    src_layout->setSpacing(Theme::Space::sm);

    auto *src_header = new QHBoxLayout();
    src_header->setContentsMargins(0, 0, 0, 0);
    src_header->setSpacing(Theme::Space::sm);
    auto *src_title = new QLabel(QStringLiteral("Source"), source_panel);
    src_title->setObjectName(QStringLiteral("workPaneTitle"));
    src_header->addWidget(src_title);
    src_header->addStretch(1);
    source_count_label_ = new QLabel(QStringLiteral("0 characters"), source_panel);
    source_count_label_->setObjectName(QStringLiteral("charCount"));
    src_header->addWidget(source_count_label_);
    clear_button_ = new QPushButton(QStringLiteral("Clear"), source_panel);
    clear_button_->setCursor(Qt::PointingHandCursor);
    clear_button_->setToolTip(QStringLiteral("Clear source and result"));
    src_header->addWidget(clear_button_);
    src_layout->addLayout(src_header);

    source_edit_ = new QPlainTextEdit(source_panel);
    source_edit_->setTabChangesFocus(true);
    src_layout->addWidget(source_edit_, 1);

    // Result pane
    auto *target_panel = new QFrame(splitter_);
    target_panel->setObjectName(QStringLiteral("workPane"));
    target_panel->setMinimumWidth(kPaneMinWidth);
    auto *tgt_layout = new QVBoxLayout(target_panel);
    tgt_layout->setContentsMargins(Theme::Space::lg, Theme::Space::sm,
                                   Theme::Space::lg, Theme::Space::lg);
    tgt_layout->setSpacing(Theme::Space::sm);

    auto *tgt_header = new QHBoxLayout();
    tgt_header->setContentsMargins(0, 0, 0, 0);
    tgt_header->setSpacing(Theme::Space::sm);
    auto *tgt_title = new QLabel(QStringLiteral("Translation"), target_panel);
    tgt_title->setObjectName(QStringLiteral("workPaneTitle"));
    tgt_header->addWidget(tgt_title);
    tgt_header->addStretch(1);
    target_count_label_ = new QLabel(QStringLiteral("0 characters"), target_panel);
    target_count_label_->setObjectName(QStringLiteral("charCount"));
    tgt_header->addWidget(target_count_label_);
    copy_button_ = new QPushButton(QStringLiteral("Copy"), target_panel);
    copy_button_->setCursor(Qt::PointingHandCursor);
    copy_button_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+C")));
#ifdef Q_OS_MACOS
    copy_button_->setToolTip(QStringLiteral("Copy translation (Cmd+Shift+C)"));
#else
    copy_button_->setToolTip(QStringLiteral("Copy translation (Ctrl+Shift+C)"));
#endif
    tgt_header->addWidget(copy_button_);
    tgt_layout->addLayout(tgt_header);

    target_edit_ = new QPlainTextEdit(target_panel);
    target_edit_->setReadOnly(true);
    target_edit_->setTabChangesFocus(true);
    target_edit_->setPlaceholderText(QStringLiteral("Translation appears here\u2026"));
    tgt_layout->addWidget(target_edit_, 1);

    // Terminal-state strip
    result_strip_ = new QWidget(target_panel);
    result_strip_->setObjectName(QStringLiteral("resultStateStrip"));
    auto *strip_layout = new QHBoxLayout(result_strip_);
    strip_layout->setContentsMargins(0, 0, 0, 0);
    strip_layout->setSpacing(Theme::Space::sm);

    result_dot_ = new QLabel(result_strip_);
    result_dot_->setObjectName(QStringLiteral("resultStateDot"));
    result_dot_->setFixedSize(Theme::Size::statusDot, Theme::Size::statusDot);
    strip_layout->addWidget(result_dot_);

    result_state_label_ = new QLabel(result_strip_);
    result_state_label_->setObjectName(QStringLiteral("resultStateText"));
    strip_layout->addWidget(result_state_label_);

    strip_layout->addStretch(1);

    retry_button_ = new QPushButton(QStringLiteral("Retry"), result_strip_);
    retry_button_->setCursor(Qt::PointingHandCursor);
    retry_button_->setToolTip(QStringLiteral("Retry the failed translation"));
    retry_button_->setVisible(false);
    strip_layout->addWidget(retry_button_);

    tgt_layout->addWidget(result_strip_);

    // Back-translate pane (only visible while enabled). Structurally mirrors
    // the Source and Translation panes: aligned title, count, and a Copy
    // action that enables once content exists. The header stays left-aligned
    // and simple: a stretch absorbs every pixel the fixed regions do not
    // need, the count hides first when the pane narrows, and only if even
    // title+button cannot fit does the title elide from the right (setText
    // then re-sizes the label). The Copy button is pinned to its sizeHint
    // (Fixed policy + trailing stretch) so it can never grow into space
    // freed by hiding the count.
    back_panel_ = new QFrame(splitter_);
    back_panel_->setObjectName(QStringLiteral("workPane"));
    back_panel_->setMinimumWidth(kPaneMinWidth);
    back_panel_->installEventFilter(this);
    auto *back_layout = new QVBoxLayout(back_panel_);
    back_layout->setContentsMargins(Theme::Space::lg, Theme::Space::sm,
                                    Theme::Space::lg, Theme::Space::lg);
    back_layout->setSpacing(Theme::Space::sm);

    auto *back_header = new QHBoxLayout();
    back_header->setContentsMargins(0, 0, 0, 0);
    back_header->setSpacing(Theme::Space::sm);
    back_title_ = new QLabel(QStringLiteral("Back translation"), back_panel_);
    back_title_->setObjectName(QStringLiteral("workPaneTitle"));
    back_title_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    back_header->addWidget(back_title_);
    // Without a stretch item, QBoxLayout hands out free space in proportion
    // to sizeHints — hiding the count would inflate the Copy button. The
    // stretch absorbs all free space so title and button keep their exact
    // sizes.
    back_header->addStretch(1);
    back_count_label_ = new QLabel(QStringLiteral("0 characters"), back_panel_);
    back_count_label_->setObjectName(QStringLiteral("charCount"));
    back_header->addWidget(back_count_label_);
    back_copy_button_ = new QPushButton(QStringLiteral("Copy"), back_panel_);
    back_copy_button_->setCursor(Qt::PointingHandCursor);
    back_copy_button_->setToolTip(QStringLiteral("Copy the back translation"));
    back_copy_button_->setAccessibleName(QStringLiteral("Copy back translation"));
    // Fixed horizontal policy: the button is exactly its sizeHint at every
    // pane width and count visibility — a constant, compact hit target.
    back_copy_button_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    back_header->addWidget(back_copy_button_);
    back_layout->addLayout(back_header);

    back_edit_ = new QPlainTextEdit(back_panel_);
    back_edit_->setReadOnly(true);
    back_edit_->setTabChangesFocus(true);
    back_edit_->setPlaceholderText(QStringLiteral("Back translation appears here\u2026"));
    back_layout->addWidget(back_edit_, 1);

    splitter_->addWidget(source_panel);
    splitter_->addWidget(target_panel);
    splitter_->addWidget(back_panel_);
    splitter_->setStretchFactor(0, 1);
    splitter_->setStretchFactor(1, 1);
    splitter_->setStretchFactor(2, 1);
    root->addWidget(splitter_, 1);

    // ── Connections ───────────────────────────────────────────────────
    connect(translate_button_, &QPushButton::clicked, this, &TranslatePage::onTranslate);
    connect(clear_button_, &QPushButton::clicked, this, &TranslatePage::onClear);
    connect(copy_button_, &QPushButton::clicked, this, &TranslatePage::onCopyResult);
    connect(back_copy_button_, &QPushButton::clicked, this, &TranslatePage::onCopyBackResult);
    connect(retry_button_, &QPushButton::clicked, this, &TranslatePage::onRetry);
    connect(back_translate_checkbox_, &QCheckBox::toggled,
            this, &TranslatePage::onBackTranslateToggled);
    connect(source_lang_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TranslatePage::languageChanged);
    connect(target_lang_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TranslatePage::languageChanged);
    connect(source_edit_, &QPlainTextEdit::textChanged,
            this, &TranslatePage::updateCharCounts);
    connect(target_edit_, &QPlainTextEdit::textChanged,
            this, &TranslatePage::updateCharCounts);
    connect(back_edit_, &QPlainTextEdit::textChanged,
            this, &TranslatePage::updateCharCounts);

    setBackTranslateVisible(false);
    updateResultStrip();
    updateActions();
}

void TranslatePage::setBusy(bool busy) {
    busy_ = busy;
    source_edit_->setReadOnly(busy || translating_);
    updateActions();
}

void TranslatePage::setTranslating(bool translating) {
    translating_ = translating;
    translate_button_->setText(translating ? QStringLiteral("Stop")
                                           : QStringLiteral("Translate"));
    source_edit_->setReadOnly(busy_ || translating_);
    if (translating) {
        // A fresh job starts: the strip reports progress until the terminal
        // outcome arrives via setTranslationResult().
        result_strip_->setProperty("stripState", QStringLiteral("translating"));
        repolish(result_strip_);
        result_state_label_->setText(QStringLiteral("Translating\u2026"));
        last_failed_ = false;
        retry_button_->setVisible(false);
    }
    updateActions();
}

void TranslatePage::setModelLoaded(bool loaded) {
    model_loaded_ = loaded;
    updateActions();
}

void TranslatePage::setTranslationResult(TranslationState state,
                                         const QString &error_message) {
    const bool failed = state == TranslationState::Failed;
    last_failed_ = failed;

    result_strip_->setProperty("stripState", stripStateFor(state));
    repolish(result_strip_);
    result_state_label_->setText(stripTextFor(state, error_message));
    updateResultStrip();
    updateActions();
}

void TranslatePage::resetTarget() {
    target_edit_->clear();
}

void TranslatePage::resetBackTranslate() {
    back_edit_->clear();
}

void TranslatePage::appendTarget(const QString &piece) {
    qtrans::log::get(qtrans::log::Component::App)
        ->debug("appendTarget len:{}", piece.size());
    target_edit_->moveCursor(QTextCursor::End);
    target_edit_->insertPlainText(piece);
    target_edit_->moveCursor(QTextCursor::End);
    target_edit_->ensureCursorVisible();
}

void TranslatePage::appendBackTranslate(const QString &piece) {
    back_edit_->moveCursor(QTextCursor::End);
    back_edit_->insertPlainText(piece);
    back_edit_->moveCursor(QTextCursor::End);
    back_edit_->ensureCursorVisible();
}

QString TranslatePage::targetText() const {
    return target_edit_->toPlainText();
}

void TranslatePage::prepareForTranslation(bool back_translate) {
    resetTarget();
    resetBackTranslate();
    setBackTranslateVisible(back_translate);
}

void TranslatePage::onBackTranslateToggled(bool enabled) {
    setBackTranslateVisible(enabled);
    if (!enabled) {
        resetBackTranslate();
    }
    // The toggle can leave the pane empty: refresh the Copy affordance.
    updateActions();
}

void TranslatePage::setBackTranslateVisible(bool visible) {
    back_panel_->setVisible(visible);
    updateSplitterOrientation();
    updateBackHeader();
}

bool TranslatePage::shouldStackPanes() const {
    if (!back_panel_->isVisible()) {
        return false;
    }
    // The splitter gets the page width minus the side margins; stack
    // vertically when three panes cannot fit side by side.
    const int splitter_width = width() - 2 * Theme::Space::xxl;
    return splitter_width < kStackThresholdWidth;
}

void TranslatePage::updateSplitterOrientation() {
    const bool stack = shouldStackPanes();
    const Qt::Orientation orientation = stack ? Qt::Vertical : Qt::Horizontal;
    if (splitter_->orientation() == orientation) {
        return;
    }

    splitter_->setOrientation(orientation);
    // Pane minimums apply along the splitter axis only: width when the
    // panes sit side by side, height when they stack.
    for (int i = 0; i < splitter_->count(); ++i) {
        QWidget *pane = splitter_->widget(i);
        if (stack) {
            pane->setMinimumWidth(0);
            pane->setMinimumHeight(kPaneMinHeight);
        } else {
            pane->setMinimumWidth(kPaneMinWidth);
            pane->setMinimumHeight(0);
        }
    }
    // Rebalance evenly after the axis change so no pane is collapsed.
    const int axis_size = stack ? splitter_->height() : splitter_->width();
    const int per_pane = axis_size / splitter_->count();
    QList<int> sizes;
    for (int i = 0; i < splitter_->count(); ++i) {
        sizes.append(per_pane);
    }
    splitter_->setSizes(sizes);
}

void TranslatePage::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    updateSplitterOrientation();
}

bool TranslatePage::eventFilter(QObject *watched, QEvent *event) {
    // The splitter can be dragged without resizing the page, so the back
    // pane's own width change is the trigger that reconciles its header.
    // Deferred: run after the layout settles so the pane's contentsRect
    // (and the label sizeHints) reflect the final geometry.
    if (watched == back_panel_ && event->type() == QEvent::Resize) {
        QMetaObject::invokeMethod(this, &TranslatePage::updateBackHeader,
                                  Qt::QueuedConnection);
        return false;
    }
    return QWidget::eventFilter(watched, event);
}

void TranslatePage::updateBackHeader() {
    // Not laid out yet (or pane hidden): leave the previous state alone.
    if (back_panel_ == nullptr || !back_panel_->isVisible() || back_panel_->width() <= 0) {
        return;
    }

    static const QString kTitle = QStringLiteral("Back translation");
    const int spacing = Theme::Space::sm;
    // Usable header width: pane contents (contentsRect already excludes the
    // QSS border) minus the pane layout's side margins.
    const int content_w = back_panel_->contentsRect().width() - 2 * Theme::Space::lg;
    const int title_full = back_title_->sizeHint().width();
    const int count_w = back_count_label_->sizeHint().width();
    const int copy_w = back_copy_button_->sizeHint().width();

    // Header items are [title][stretch][count][copy]: three spacing gaps
    // while the count is visible, two once it hides.
    const int gaps_with_count = 3;
    const int gaps_without_count = 2;

    // Priority order: fixed Copy button first, then the full title, and the
    // character count is the first thing to hide as the pane narrows.
    const bool count_visible =
        content_w >= title_full + count_w + copy_w + gaps_with_count * spacing;
    back_count_label_->setVisible(count_visible);

    // The title elides from the right only as a last resort, when even
    // title+button cannot fit. setText updates the label's sizeHint, so the
    // layout gives the label exactly what it needs and the Copy button is
    // never displaced nor overlapped (the stretch absorbs the remainder).
    const int title_budget = content_w - copy_w - gaps_without_count * spacing;
    if (title_full <= title_budget) {
        back_title_->setText(kTitle);
    } else {
        const QFontMetrics metrics(back_title_->font());
        back_title_->setText(
            metrics.elidedText(kTitle, Qt::ElideRight,
                               qMax(Theme::Space::xl, title_budget)));
    }
}

void TranslatePage::onTranslate() {
    if (translating_) {
        emit cancelRequested();
        return;
    }

    // The Translate button is disabled without a loaded model; the empty
    // source check is a silent no-op — the page owns no local status
    // surface to report either case.
    if (!model_loaded_ || source_edit_->toPlainText().trimmed().isEmpty()) {
        return;
    }

    // Retain the request so a failure can offer Retry with the exact same
    // source, languages, and back-translate flag.
    last_source_ = source_edit_->toPlainText();
    last_target_language_ = targetLanguageName();
    last_source_language_ = sourceLanguageName();
    last_back_translate_ = back_translate_checkbox_->isChecked();

    prepareForTranslation(last_back_translate_);

    emit translateRequested(
        last_source_,
        last_target_language_,
        last_source_language_,
        last_back_translate_);
}

void TranslatePage::onRetry() {
    if (translating_ || busy_) {
        return;
    }
    if (!model_loaded_ || last_source_.trimmed().isEmpty()) {
        return;
    }

    prepareForTranslation(last_back_translate_);
    emit translateRequested(
        last_source_,
        last_target_language_,
        last_source_language_,
        last_back_translate_);
}

void TranslatePage::onClear() {
    source_edit_->clear();
    target_edit_->clear();
    back_edit_->clear();
    last_failed_ = false;
    // Clear the terminal-state strip (text + state tint) so a prior outcome
    // never lingers after Clear; updateResultStrip() then hides its contents.
    result_state_label_->clear();
    result_strip_->setProperty("stripState", QStringLiteral("idle"));
    repolish(result_strip_);
    updateResultStrip();
    updateActions();
}

void TranslatePage::onCopyResult() {
    QApplication::clipboard()->setText(target_edit_->toPlainText());
}

void TranslatePage::onCopyBackResult() {
    QApplication::clipboard()->setText(back_edit_->toPlainText());
}

QString TranslatePage::targetLanguageName() const {
    return languageNameAt(target_lang_combo_);
}

QString TranslatePage::sourceLanguageName() const {
    return languageNameAt(source_lang_combo_);
}

void TranslatePage::setSourceLanguage(const QString &model_name) {
    const QSignalBlocker blocker(source_lang_combo_);
    const int idx = source_lang_combo_->findText(model_name);
    if (idx >= 0) {
        source_lang_combo_->setCurrentIndex(idx);
    }
}

void TranslatePage::setTargetLanguage(const QString &model_name) {
    const QSignalBlocker blocker(target_lang_combo_);
    const int idx = target_lang_combo_->findText(model_name);
    if (idx >= 0) {
        target_lang_combo_->setCurrentIndex(idx);
    }
}

void TranslatePage::updateCharCounts() {
    source_count_label_->setText(charCountText(source_edit_->toPlainText()));
    target_count_label_->setText(charCountText(target_edit_->toPlainText()));
    back_count_label_->setText(charCountText(back_edit_->toPlainText()));
}

void TranslatePage::updateResultStrip() {
    // The strip participates in the layout at all times (min-height in QSS)
    // but only shows its dot/label while a job is running or has finished.
    const bool active = translating_ ||
                        last_failed_ ||
                        !result_state_label_->text().isEmpty();
    result_dot_->setVisible(active);
    result_state_label_->setVisible(active);
    retry_button_->setVisible(last_failed_ && !busy_ && !translating_);
}

void TranslatePage::updateActions() {
    if (translating_) {
        translate_button_->setEnabled(true);
    } else {
        translate_button_->setEnabled(!busy_ && model_loaded_);
    }
    clear_button_->setEnabled(!busy_ && !translating_);
    copy_button_->setEnabled(!busy_ && !translating_ &&
                             !target_edit_->toPlainText().isEmpty());
    back_copy_button_->setEnabled(!busy_ && !translating_ &&
                                  !back_edit_->toPlainText().isEmpty());
    source_lang_combo_->setEnabled(!busy_ && !translating_);
    target_lang_combo_->setEnabled(!busy_ && !translating_);
    back_translate_checkbox_->setEnabled(!busy_ && !translating_);
    retry_button_->setEnabled(!busy_ && !translating_);
}
