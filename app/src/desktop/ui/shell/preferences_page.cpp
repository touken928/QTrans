#include "ui/shell/preferences_page.h"
#include "ui/shared/theme/theme.h"
#include "shared/string_bridge.h"
#include "ui/shared/widget_utils.h"
#include "domain/model-catalog/language_list.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QFrame>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStyle>
#include <QVBoxLayout>

namespace {

int findLanguageIndex(const QString &model_name) {
    for (int i = 0; i < translation_language_count(); ++i) {
        if (qtrans::app::from_utf8(translation_languages()[i].model_name) == model_name) {
            return i;
        }
    }
    return 0;
}

QString modelNameAt(const QComboBox *combo) {
    const int index = combo->currentIndex();
    if (index < 0 || index >= translation_language_count()) {
        return QStringLiteral("English");
    }
    return qtrans::app::from_utf8(translation_languages()[index].model_name);
}

QString apiEndpointText(int port) {
    return QStringLiteral("http://127.0.0.1:%1/v1").arg(port);
}

// A shortcut is only usable as a global hotkey when it carries a modifier
// (or is a bare function key); a naked letter would hijack typing
// everywhere. Single-sequence keys only.
bool isUsableShortcut(const QKeySequence &sequence) {
    if (sequence.isEmpty() || sequence.count() != 1) {
        return false;
    }
    const QKeyCombination combo = sequence[0];
    if (combo.keyboardModifiers() != Qt::NoModifier) {
        return true;
    }
    const Qt::Key key = static_cast<Qt::Key>(combo.key());
    return key >= Qt::Key_F1 && key <= Qt::Key_F35;
}

// Refreshes a dynamic-property selector (e.g. [level="error"]) after the
// property changed so the stylesheet retints the label immediately.
void repolish(QWidget *widget) {
    if (widget != nullptr && widget->style() != nullptr) {
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
    }
}

}  // namespace

PreferencesPage::PreferencesPage(QWidget *parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("page"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Fixed header (carries the side/top padding) ───────────────────
    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("preferencesHeader"));
    auto *header_layout = new QVBoxLayout(header);
    header_layout->setContentsMargins(Theme::Space::xxl, Theme::Space::xl,
                                      Theme::Space::xxl, Theme::Space::lg);
    header_layout->setSpacing(Theme::Space::xs);

    auto *title = new QLabel(QStringLiteral("Preferences"), header);
    title->setObjectName(QStringLiteral("sectionTitle"));
    header_layout->addWidget(title);

    auto *subtitle = new QLabel(
        QStringLiteral("Word selection, hotkey, and local API settings."), header);
    subtitle->setObjectName(QStringLiteral("mutedLabel"));
    header_layout->addWidget(subtitle);

    root->addWidget(header);

    // ── Scrollable sections ───────────────────────────────────────────
    auto *scroll = new QScrollArea(this);
    // Object name anchors the QSS rule that keeps the scroll surface on the
    // app neutral page background (see preferencesQss in app_theme.cpp) —
    // without it the viewport/content chain paints palette Base (white).
    scroll->setObjectName(QStringLiteral("preferencesScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setAutoFillBackground(false);

    auto *content = new QWidget(scroll);
    auto *content_layout = new QVBoxLayout(content);
    // The header owns the top/side padding; this container adds the bottom
    // breathing room and the side gutter for the scrolling region.
    content_layout->setContentsMargins(Theme::Space::xxl, Theme::Space::lg,
                                       Theme::Space::xxl, Theme::Space::xxl);
    content_layout->setSpacing(Theme::Space::lg);
    content_layout->addStretch(1);

    auto make_card = [content]() {
        auto *card = new QFrame(content);
        card->setObjectName(QStringLiteral("settingsSection"));
        auto *form = new QFormLayout(card);
        form->setContentsMargins(Theme::Space::xl, Theme::Space::lg,
                                 Theme::Space::xl, Theme::Space::lg);
        form->setSpacing(Theme::Space::md);
        form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        return card;
    };

    // ── General ───────────────────────────────────────────────────────
    auto *general_card = make_card();
    auto *general_form = qobject_cast<QFormLayout *>(general_card->layout());

    auto *general_title = new QLabel(QStringLiteral("General"), general_card);
    general_title->setObjectName(QStringLiteral("sectionTitle"));
    general_form->addRow(general_title);

    close_to_tray_checkbox_ = new QCheckBox(
        QStringLiteral("Close to system tray instead of quitting"), general_card);
    general_form->addRow(QString(), close_to_tray_checkbox_);

    content_layout->addWidget(general_card);

    // ── Word Selection ────────────────────────────────────────────────
    auto *word_card = make_card();
    auto *word_form = qobject_cast<QFormLayout *>(word_card->layout());

    auto *word_title = new QLabel(QStringLiteral("Word Selection"), word_card);
    word_title->setObjectName(QStringLiteral("sectionTitle"));
    word_form->addRow(word_title);

    enabled_checkbox_ = new QCheckBox(
        QStringLiteral("Enable word selection translation"), word_card);
    word_form->addRow(QString(), enabled_checkbox_);

    target_lang_combo_ = new QComboBox(word_card);
    for (int i = 0; i < translation_language_count(); ++i) {
        target_lang_combo_->addItem(qtrans::app::from_utf8(translation_languages()[i].label));
    }
    target_lang_combo_->setCurrentIndex(findLanguageIndex(QStringLiteral("Chinese")));
    configureComboBox(target_lang_combo_, 200);

    auto *target_label = new QLabel(QStringLiteral("Target language:"), word_card);
    target_label->setObjectName(QStringLiteral("formLabel"));
    word_form->addRow(target_label, target_lang_combo_);

    hotkey_edit_ = new QKeySequenceEdit(word_card);
    hotkey_edit_->setClearButtonEnabled(true);
    hotkey_edit_->setToolTip(
        QStringLiteral("Click and press the keys to capture the global shortcut"));

    auto *hotkey_label = new QLabel(QStringLiteral("Shortcut:"), word_card);
    hotkey_label->setObjectName(QStringLiteral("formLabel"));
    word_form->addRow(hotkey_label, hotkey_edit_);

    auto_close_spin_ = new QSpinBox(word_card);
    auto_close_spin_->setRange(1000, 30000);
    auto_close_spin_->setSingleStep(500);
    auto_close_spin_->setSuffix(QStringLiteral(" ms"));
    auto_close_spin_->setValue(5000);

    auto *auto_close_label = new QLabel(QStringLiteral("Auto-close popup after:"), word_card);
    auto_close_label->setObjectName(QStringLiteral("formLabel"));
    word_form->addRow(auto_close_label, auto_close_spin_);

    auto *usage_hint = new QLabel(
        QStringLiteral("Select text in any app, then press the shortcut to translate it "
                       "in a popup."),
        word_card);
    usage_hint->setObjectName(QStringLiteral("mutedLabel"));
    usage_hint->setWordWrap(true);
    word_form->addRow(QString(), usage_hint);

    feedback_label_ = new QLabel(word_card);
    feedback_label_->setObjectName(QStringLiteral("settingsFeedback"));
    feedback_label_->setWordWrap(true);
    feedback_label_->setVisible(false);
    word_form->addRow(QString(), feedback_label_);

    content_layout->addWidget(word_card);

    // ── Integrations / Local API ──────────────────────────────────────
    auto *api_card = make_card();
    auto *api_form = qobject_cast<QFormLayout *>(api_card->layout());

    auto *api_title = new QLabel(QStringLiteral("Integrations"), api_card);
    api_title->setObjectName(QStringLiteral("sectionTitle"));
    api_form->addRow(api_title);

    api_checkbox_ = new QCheckBox(
        QStringLiteral("Enable local API (OpenAI-compatible endpoint)"), api_card);
    api_form->addRow(QString(), api_checkbox_);

    api_port_spin_ = new QSpinBox(api_card);
    api_port_spin_->setRange(1024, 65535);
    api_port_spin_->setValue(8000);

    auto *port_label = new QLabel(QStringLiteral("Port:"), api_card);
    port_label->setObjectName(QStringLiteral("formLabel"));
    api_form->addRow(port_label, api_port_spin_);

    endpoint_edit_ = new QLineEdit(api_card);
    endpoint_edit_->setReadOnly(true);
    endpoint_edit_->setText(apiEndpointText(api_port_spin_->value()));
    endpoint_edit_->setToolTip(QStringLiteral("Local endpoint URL"));
    endpoint_edit_->setAccessibleName(QStringLiteral("Local API endpoint"));

    endpoint_copy_button_ = new QPushButton(QStringLiteral("Copy"), api_card);
    endpoint_copy_button_->setCursor(Qt::PointingHandCursor);
    endpoint_copy_button_->setToolTip(QStringLiteral("Copy the endpoint URL"));

    auto *endpoint_row = new QHBoxLayout();
    endpoint_row->setContentsMargins(0, 0, 0, 0);
    endpoint_row->setSpacing(Theme::Space::sm);
    endpoint_row->addWidget(endpoint_edit_, 1);
    endpoint_row->addWidget(endpoint_copy_button_);

    auto *endpoint_label = new QLabel(QStringLiteral("Endpoint:"), api_card);
    endpoint_label->setObjectName(QStringLiteral("formLabel"));
    api_form->addRow(endpoint_label, endpoint_row);

    auto *api_hint = new QLabel(
        QStringLiteral("Serves the loaded model through a local HTTP endpoint at the "
                       "address above."),
        api_card);
    api_hint->setObjectName(QStringLiteral("mutedLabel"));
    api_hint->setWordWrap(true);
    api_form->addRow(QString(), api_hint);

    content_layout->addWidget(api_card);

    // ── Advanced ──────────────────────────────────────────────────────
    auto *advanced_card = make_card();
    auto *advanced_form = qobject_cast<QFormLayout *>(advanced_card->layout());

    auto *advanced_title = new QLabel(QStringLiteral("Advanced"), advanced_card);
    advanced_title->setObjectName(QStringLiteral("sectionTitle"));
    advanced_form->addRow(advanced_title);

    auto *data_dir_edit = new QLineEdit(advanced_card);
    data_dir_edit->setReadOnly(true);
    data_dir_edit->setPlaceholderText(QStringLiteral("App data directory"));
    data_dir_edit->setToolTip(QStringLiteral("Where QTrans stores its data"));
    data_dir_edit_ = data_dir_edit;

    auto *data_label = new QLabel(QStringLiteral("Data directory:"), advanced_card);
    data_label->setObjectName(QStringLiteral("formLabel"));
    advanced_form->addRow(data_label, data_dir_edit);

    auto *advanced_hint = new QLabel(
        QStringLiteral("Logs, batch output, and app data live here. Model files are "
                       "managed on the Models page."),
        advanced_card);
    advanced_hint->setObjectName(QStringLiteral("mutedLabel"));
    advanced_hint->setWordWrap(true);
    advanced_form->addRow(QString(), advanced_hint);

    content_layout->addWidget(advanced_card);

    scroll->setWidget(content);
    root->addWidget(scroll, 1);

    // ── Connections ───────────────────────────────────────────────────
    connect(close_to_tray_checkbox_, &QCheckBox::toggled,
            this, &PreferencesPage::settingsChanged);
    connect(enabled_checkbox_, &QCheckBox::toggled,
            this, &PreferencesPage::settingsChanged);
    connect(target_lang_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PreferencesPage::settingsChanged);
    connect(hotkey_edit_, &QKeySequenceEdit::editingFinished,
            this, &PreferencesPage::onHotkeyEdited);
    connect(auto_close_spin_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &PreferencesPage::settingsChanged);
    connect(api_checkbox_, &QCheckBox::toggled,
            this, &PreferencesPage::settingsChanged);
    // The API port restarts the local service on settingsChanged, so emit
    // only on committed edits; the endpoint row still tracks the value live.
    connect(api_port_spin_, &QSpinBox::editingFinished,
            this, &PreferencesPage::settingsChanged);
    connect(api_port_spin_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int port) { updateEndpoint(); });
    connect(endpoint_copy_button_, &QPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(endpoint_edit_->text());
        setFeedback(QStringLiteral("Endpoint copied to clipboard"), false);
    });
}

// ── General ────────────────────────────────────────────────────────────────

void PreferencesPage::setCloseToTray(bool close_to_tray) {
    const QSignalBlocker blocker(close_to_tray_checkbox_);
    close_to_tray_checkbox_->setChecked(close_to_tray);
}

bool PreferencesPage::isCloseToTray() const {
    return close_to_tray_checkbox_->isChecked();
}

// ── Word Selection ────────────────────────────────────────────────────────

void PreferencesPage::setEnabled(bool enabled) {
    const QSignalBlocker blocker(enabled_checkbox_);
    enabled_checkbox_->setChecked(enabled);
}

bool PreferencesPage::isEnabled() const {
    return enabled_checkbox_->isChecked();
}

void PreferencesPage::setTargetLanguage(const QString &model_name) {
    const QSignalBlocker blocker(target_lang_combo_);
    const int idx = findLanguageIndex(model_name);
    if (idx >= 0) {
        target_lang_combo_->setCurrentIndex(idx);
    }
}

QString PreferencesPage::targetLanguage() const {
    return modelNameAt(target_lang_combo_);
}

void PreferencesPage::setHotkey(const QString &shortcut) {
    // setKeySequence does not fire editingFinished, so programmatic
    // rollback can never re-trigger settingsChanged.
    hotkey_edit_->setKeySequence(QKeySequence(shortcut.trimmed()));
}

QString PreferencesPage::hotkey() const {
    return hotkey_edit_->keySequence().toString(QKeySequence::PortableText);
}

void PreferencesPage::setAutoCloseMs(int ms) {
    const QSignalBlocker blocker(auto_close_spin_);
    auto_close_spin_->setValue(ms);
}

int PreferencesPage::autoCloseMs() const {
    return auto_close_spin_->value();
}

// ── Integrations / Local API ──────────────────────────────────────────────

void PreferencesPage::setApiEnabled(bool enabled) {
    const QSignalBlocker blocker(api_checkbox_);
    api_checkbox_->setChecked(enabled);
}

bool PreferencesPage::isApiEnabled() const {
    return api_checkbox_->isChecked();
}

void PreferencesPage::setApiPort(int port) {
    const QSignalBlocker blocker(api_port_spin_);
    api_port_spin_->setValue(port);
    updateEndpoint();
}

int PreferencesPage::apiPort() const {
    return api_port_spin_->value();
}

// ── Advanced ──────────────────────────────────────────────────────────────

void PreferencesPage::setDataDirectory(const QString &path) {
    data_dir_edit_->setText(path);
    data_dir_edit_->setToolTip(path);
}

// ── Feedback ──────────────────────────────────────────────────────────────

void PreferencesPage::setFeedback(const QString &message, bool error) {
    feedback_label_->setProperty("level", error ? QStringLiteral("error")
                                                : QStringLiteral("success"));
    repolish(feedback_label_);
    feedback_label_->setText(message);
    feedback_label_->setVisible(!message.isEmpty());
}

void PreferencesPage::onHotkeyEdited() {
    // Shape validation happens here with immediate local feedback; the
    // actual registration check (and rollback) is MainWindow's job because
    // it owns the HotkeyManager and persistence.
    const QKeySequence sequence = hotkey_edit_->keySequence();
    if (!isUsableShortcut(sequence)) {
        setFeedback(
            QStringLiteral("A shortcut with a modifier key is required, e.g. Ctrl+`."),
            true);
        return;
    }
    setFeedback(QString{}, false);
    emit settingsChanged();
}

void PreferencesPage::updateEndpoint() {
    endpoint_edit_->setText(apiEndpointText(api_port_spin_->value()));
}
