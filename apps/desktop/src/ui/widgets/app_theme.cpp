#include "ui/widgets/app_theme.h"
#include "ui/theme.h"

#include <QWidget>

// =============================================================================
// Internal helpers — each returns a QSS fragment for one widget family.
// All dimensions / colours come from Theme:: so that a single change in
// theme.h propagates everywhere.
// =============================================================================

namespace {

// -- Buttons ----------------------------------------------------------------

QString buttonQss() {
    namespace C = Theme::Color;
    return QStringLiteral(
               "QPushButton {"
               "  background-color: %1;"
               "  border: 1px solid %2;"
               "  border-radius: %3px;"
               "  padding: 8px 16px;"
               "  color: %4;"
               "  font-size: %5px;"
               "}"
               "QPushButton:hover {"
               "  background-color: %6;"
               "  border-color: %7;"
               "}"
               "QPushButton:pressed {"
               "  background-color: %8;"
               "}"
               "QPushButton:disabled {"
               "  color: %9;"
               "  background-color: %6;"
               "  border-color: %2;"
               "}"
               "QPushButton#primaryButton,"
               "QPushButton#translateButton {"
               "  background-color: %10;"
               "  border-color: %10;"
               "  color: %11;"
               "  font-weight: bold;"
               "}"
               "QPushButton#primaryButton:hover,"
               "QPushButton#translateButton:hover {"
               "  background-color: %12;"
               "  border-color: %12;"
               "}"
               "QPushButton#primaryButton:pressed,"
               "QPushButton#translateButton:pressed {"
               "  background-color: %13;"
               "  border-color: %13;"
               "  color: %11;"
               "}"
               "QPushButton#primaryButton:disabled,"
               "QPushButton#translateButton:disabled {"
               "  background-color: %9;"
               "  border-color: %9;"
               "  color: %11;"
               "}"
               "QPushButton#dangerButton {"
               "  background-color: transparent;"
               "  border-color: %14;"
               "  color: %14;"
               "}"
               "QPushButton#dangerButton:hover {"
               "  background-color: %14;"
               "  color: %11;"
               "}")
        .arg(C::surface)
        .arg(C::border)
        .arg(Theme::Radius::sm)
        .arg(C::text)
        .arg(Theme::Font::md)
        .arg(C::hover)
        .arg(C::border)
        .arg(C::pressed)
        .arg(C::textDisabled)
        .arg(C::primary)
        .arg(C::surface)
        .arg(C::primaryHover)
        .arg(C::primaryPressed)
        .arg(C::danger);
}

// -- Drop-down lists --------------------------------------------------------

QString comboBoxQss() {
    namespace C = Theme::Color;
    return QStringLiteral(
               "QComboBox {"
               "  background-color: %1;"
               "  border: 1px solid %2;"
               "  border-radius: %3px;"
               "  padding: 6px 30px 6px 10px;"
               "  min-height: 24px;"
               "  color: %4;"
               "  font-size: %5px;"
               "}"
               "QComboBox:disabled {"
               "  color: %6;"
               "  background-color: %7;"
               "}"
               "QComboBox::drop-down {"
               "  subcontrol-origin: padding;"
               "  subcontrol-position: top right;"
               "  width: 26px;"
               "  border: none;"
               "  border-left: 1px solid %2;"
               "  border-top-right-radius: %3px;"
               "  border-bottom-right-radius: %3px;"
               "  background-color: %1;"
               "}"
               "QComboBox QAbstractItemView {"
               "  background-color: %1;"
               "  color: %4;"
               "  border: 1px solid %2;"
               "  border-radius: %3px;"
               "  padding: %8px;"
               "  outline: none;"
               "  selection-background-color: %9;"
               "  selection-color: %1;"
               "}"
               "QComboBox QAbstractItemView::item {"
               "  min-height: 30px;"
               "  padding: 6px 12px;"
               "}"
               "QComboBox QAbstractItemView::item:hover {"
               "  background-color: %7;"
               "  color: %4;"
               "}")
        .arg(C::surface)
        .arg(C::border)
        .arg(Theme::Radius::sm)
        .arg(C::text)
        .arg(Theme::Font::md)
        .arg(C::textDisabled)
        .arg(C::hover)
        .arg(Theme::Space::xs)
        .arg(C::selection);
}

// -- Context menus ----------------------------------------------------------

QString menuQss() {
    namespace C = Theme::Color;
    return QStringLiteral(
               "QMenu {"
               "  background-color: %1;"
               "  border: 1px solid %2;"
               "  border-radius: %3px;"
               "  padding: %8px;"
               "}"
               "QMenu::item {"
               "  padding: 6px 24px;"
               "  border-radius: %9px;"
               "}"
               "QMenu::item:selected {"
               "  background-color: %10;"
               "  color: %1;"
               "}"
               "QMenu::item:disabled {"
               "  color: %7;"
               "}"
               "QMenu::separator {"
               "  height: 1px;"
               "  background-color: %2;"
               "  margin: 4px 8px;"
               "}")
        .arg(C::surface)
        .arg(C::border)
        .arg(Theme::Radius::sm)
        .arg(C::textDisabled)
        .arg(Theme::Space::xs)
        .arg(Theme::Radius::xs)
        .arg(C::selection);
}

// -- Text inputs (QLineEdit, QPlainTextEdit) ---------------------------------

QString inputQss() {
    namespace C = Theme::Color;
    return QStringLiteral(
               "QLineEdit, QPlainTextEdit {"
               "  background-color: %1;"
               "  border: 1px solid %2;"
               "  border-radius: %3px;"
               "  padding: 8px 10px;"
               "  selection-background-color: %4;"
               "  selection-color: %1;"
               "  color: %5;"
               "  font-size: %6px;"
               "}"
               "QPlainTextEdit:focus {"
               "  border-color: %4;"
               "}"
               "QLineEdit:focus {"
               "  border-color: %4;"
               "}")
        .arg(C::surface)
        .arg(C::border)
        .arg(Theme::Radius::sm)
        .arg(C::selection)
        .arg(C::text)
        .arg(Theme::Font::lg);
}

// -- Checkboxes -------------------------------------------------------------

QString checkboxQss() {
    namespace C = Theme::Color;
    return QStringLiteral(
               "QCheckBox {"
               "  spacing: 8px;"
               "  color: %4;"
               "  font-size: %5px;"
               "}"
               "QCheckBox::indicator {"
               "  width: 18px;"
               "  height: 18px;"
               "  border: 1px solid %3;"
               "  border-radius: %6px;"
               "  background-color: %1;"
               "}"
               "QCheckBox::indicator:checked {"
               "  background-color: %2;"
               "  border-color: %2;"
               "}"
               "QCheckBox:disabled {"
               "  color: %3;"
               "}")
        .arg(C::surface)
        .arg(C::primary)
        .arg(C::textDisabled)
        .arg(C::text)
        .arg(Theme::Font::md)
        .arg(Theme::Radius::xs);
}

// -- Spin boxes -------------------------------------------------------------

QString spinBoxQss() {
    namespace C = Theme::Color;
    return QStringLiteral(
               "QSpinBox {"
               "  background-color: %1;"
               "  border: 1px solid %2;"
               "  border-radius: %3px;"
               "  padding: 6px 8px;"
               "  min-height: 24px;"
               "  color: %4;"
               "  font-size: %5px;"
               "}"
               "QSpinBox:disabled {"
               "  color: %6;"
               "  background-color: %7;"
               "}"
               "QSpinBox::up-button, QSpinBox::down-button {"
               "  subcontrol-origin: border;"
               "  width: 24px;"
               "  border: none;"
               "}")
        .arg(C::surface)
        .arg(C::border)
        .arg(Theme::Radius::sm)
        .arg(C::text)
        .arg(Theme::Font::md)
        .arg(C::textDisabled)
        .arg(C::hover);
}

// -- Progress bars ----------------------------------------------------------

QString progressBarQss() {
    namespace C = Theme::Color;
    return QStringLiteral(
               "QProgressBar {"
               "  background-color: %1;"
               "  border: 1px solid %2;"
               "  border-radius: %3px;"
               "  text-align: center;"
               "  min-height: 8px;"
               "}"
               "QProgressBar::chunk {"
               "  background-color: %4;"
               "  border-radius: %5px;"
               "}")
        .arg(C::progressTrack)
        .arg(C::border)
        .arg(Theme::Radius::sm)
        .arg(C::primary)
        .arg(Theme::Radius::sm);
}

// -- Scroll bars ------------------------------------------------------------

QString scrollBarQss() {
    namespace C = Theme::Color;
    return QStringLiteral(
               "QScrollBar:vertical {"
               "  background-color: transparent;"
               "  width: 8px;"
               "  margin: 0;"
               "}"
               "QScrollBar::handle:vertical {"
               "  background-color: %1;"
               "  border-radius: 4px;"
               "  min-height: 30px;"
               "}"
               "QScrollBar::handle:vertical:hover {"
               "  background-color: %2;"
               "}"
               "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
               "  height: 0;"
               "}"
               "QScrollBar:horizontal {"
               "  background-color: transparent;"
               "  height: 8px;"
               "  margin: 0;"
               "}"
               "QScrollBar::handle:horizontal {"
               "  background-color: %1;"
               "  border-radius: 4px;"
               "  min-width: 30px;"
               "}"
               "QScrollBar::handle:horizontal:hover {"
               "  background-color: %2;"
               "}"
               "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {"
               "  width: 0;"
               "}")
        .arg(C::border)
        .arg(C::textDisabled);
}

// -- Splitter ---------------------------------------------------------------

QString splitterQss() {
    return QStringLiteral(
        "QSplitter::handle {"
        "  background-color: transparent;"
        "}"
        "QSplitter::handle:horizontal {"
        "  width: 16px;"
        "}"
        "QSplitter::handle:horizontal:hover {"
        "  background-color: transparent;"
        "}");
}

// -- Sidebar nav buttons ----------------------------------------------------

QString navButtonQss() {
    namespace C = Theme::Color;
    return QStringLiteral(
               "QPushButton#navButton {"
               "  text-align: left;"
               "  padding: 10px 16px;"
               "  border: none;"
               "  border-radius: %1px;"
               "  background-color: transparent;"
               "  color: %2;"
               "  font-size: %3px;"
               "  icon-size: 16px;"
               "}"
               "QPushButton#navButton:hover {"
               "  background-color: %4;"
               "}"
               "QPushButton#navButton:checked {"
               "  background-color: %5;"
               "  color: %6;"
               "  font-weight: bold;"
               "}")
        .arg(Theme::Radius::md)
        .arg(Theme::Color::text)
        .arg(Theme::Font::lg)
        .arg(C::hover)
        .arg(C::primary)
        .arg(C::surface);
}

// -- Popup window (word-select result) --------------------------------------

QString popupFrameQss() {
    namespace C = Theme::Color;
    return QStringLiteral(
               "QFrame#popupFrame {"
               "  background-color: %1;"
               "  border: 1px solid %2;"
               "  border-radius: %3px;"
               "}"
               "QLabel#popupResult {"
               "  color: %4;"
               "  font-size: %5px;"
               "  padding: 2px 0;"
               "  background: transparent;"
               "}"
               "QLabel#popupStatus {"
               "  color: %6;"
               "  font-size: %7px;"
               "  padding: 2px 0;"
               "  background: transparent;"
               "}"
               "QPushButton#popupCopyBtn {"
               "  background-color: transparent;"
               "  border: none;"
               "  color: %8;"
               "  font-size: %7px;"
               "  padding: 0 6px;"
               "}"
               "QPushButton#popupCopyBtn:hover {"
               "  color: %9;"
               "}"
               "QPushButton#popupCloseBtn {"
               "  background-color: transparent;"
               "  border: none;"
               "  color: %10;"
               "  font-size: %7px;"
               "  padding: 0;"
               "}"
               "QPushButton#popupCloseBtn:hover {"
               "  color: %11;"
               "}")
        .arg(C::surface)
        .arg(C::border)
        .arg(Theme::Radius::md)
        .arg(C::text)
        .arg(Theme::Font::lg)
        .arg(C::textMuted)
        .arg(Theme::Font::xs)
        .arg(C::primary)
        .arg(C::primaryHover)
        .arg(C::danger)
        .arg(C::dangerHover);
}

// -- Modal dialog panel -----------------------------------------------------

QString modalPanelQss() {
    namespace C = Theme::Color;
    return QStringLiteral(
               "QFrame#modalPanel {"
               "  background-color: %1;"
               "  border: 1px solid %2;"
               "  border-radius: %3px;"
               "  color: %4;"
               "}"
               "QFrame#modalPanel QLabel {"
               "  background-color: transparent;"
               "  color: %4;"
               "}"
               "QFrame#modalPanel QLabel#mutedLabel {"
               "  color: %5;"
               "}"
               "QFrame#modalPanel QLabel#titleLabel {"
               "  font-size: %6px;"
               "  font-weight: bold;"
               "}")
        .arg(C::surface)
        .arg(C::border)
        .arg(Theme::Radius::lg)
        .arg(C::text)
        .arg(C::textMuted)
        .arg(Theme::Font::title);
}

}  // namespace

// =============================================================================
// AppTheme  — public API
// =============================================================================

namespace AppTheme {

QString applicationStyleSheet() {
    namespace C = Theme::Color;
    namespace S = Theme::Space;

    return QStringLiteral(
               "/* ── Root ── */"
               "QWidget {"
               "  color: %1;"
               "}"
               ""
               "/* ── Window structure ── */"
               "QMainWindow, QWidget#centralRoot {"
               "  background-color: %2;"
               "}"
               "QStackedWidget {"
               "  background-color: transparent;"
               "}"
               ""
               "/* ── Sidebar ── */"
               "QWidget#sidebar {"
               "  background-color: %3;"
               "  border-right: 1px solid %4;"
               "}"
               "QLabel#sidebarLogo {"
               "  background: transparent;"
               "  margin: 0;"
               "  padding: 0;"
               "}"
               ""
               "/* ── Labels ── */"
               "QLabel#panelLabel {"
               "  color: %5;"
               "  font-size: %6px;"
               "  font-weight: bold;"
               "  padding-bottom: %7px;"
               "}"
               "QLabel#statusLabel {"
               "  color: %5;"
               "  font-size: %6px;"
               "}"
               "QLabel#sectionTitle {"
               "  font-size: %8px;"
               "  font-weight: bold;"
               "  color: %9;"
               "  padding: 0;"
               "}"
               ""
               "/* ── Page content cards ── */"
               "QFrame#pageCard {"
               "  background-color: %10;"
               "  border: 1px solid %4;"
               "  border-radius: %11px;"
               "}"
               ""
               "/* ── Toolbar ── */"
               "QWidget#translateToolbar {"
               "  background-color: %10;"
               "  border: 1px solid %4;"
               "  border-radius: %11px;"
               "  padding: %15px;"
               "}"
               ""
               "/* ── Footer ── */"
               "QWidget#pageFooter {"
               "  background-color: transparent;"
               "  padding-top: %7px;"
               "}"
               ""
               "/* ── Form rows ── */"
               "QLabel#formLabel {"
               "  color: %9;"
               "  font-size: %16px;"
               "  font-weight: bold;"
               "  padding: 0;"
               "}"
               ""
               "/* ── Settings sections ── */"
               "QFrame#settingsSection {"
               "  background-color: %10;"
               "  border: 1px solid %4;"
               "  border-radius: %11px;"
               "}"
               ""
               "/* ── Model cards ── */"
               "QFrame#modelCard {"
               "  background-color: %10;"
               "  border: 1px solid %4;"
               "  border-radius: %11px;"
               "}"
               "QFrame#modelCard:hover {"
               "  border-color: %12;"
               "}"

               "QLabel#modelCardName {"
               "  font-size: %14px;"
               "  font-weight: bold;"
               "  color: %9;"
               "}"
               "QLabel#modelCardFile {"
               "  font-size: %6px;"
               "  color: %5;"
               "}"
               ""
               "/* ── Scroll area ── */"
               "QWidget#modelCardsContainer {"
               "  background-color: transparent;"
               "}")
               .arg(C::text)
               .arg(C::bg)
               .arg(C::sidebar)
               .arg(C::border)
               .arg(C::textMuted)
               .arg(Theme::Font::sm)
               .arg(S::xs)
               .arg(Theme::Font::xl)
               .arg(C::text)
               .arg(C::surface)
               .arg(Theme::Radius::md)
               .arg(C::primary)
               .arg(Theme::Font::xl)
               .arg(S::md)
               .arg(Theme::Font::md)
           // Sub-stylesheets — order does not matter for the final cascade.
           + buttonQss() + comboBoxQss() + menuQss() + inputQss() + checkboxQss() + spinBoxQss() + progressBarQss() + scrollBarQss() + splitterQss() + navButtonQss();
}

QString modalOverlayStyleSheet() {
    return QStringLiteral("#modalOverlay { background-color: %1; }")
        .arg(Theme::Color::overlay);
}

QString modalPanelStyleSheet() {
    return modalPanelQss() + buttonQss() + comboBoxQss() + menuQss();
}

QString popupWindowStyleSheet() {
    return popupFrameQss();
}

void apply(QWidget *widget) {
    if (widget != nullptr) {
        widget->setStyleSheet(applicationStyleSheet());
    }
}

void applyPopup(QWidget *widget) {
    if (widget != nullptr) {
        widget->setStyleSheet(popupWindowStyleSheet());
    }
}

}  // namespace AppTheme
