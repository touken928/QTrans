#include "ui/shared/theme/app_theme.h"
#include "ui/shared/theme/theme.h"

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

// -- Sidebar nav buttons (icon rail) ------------------------------------

QString navButtonQss() {
    namespace C = Theme::Color;
    return QStringLiteral(
               "QToolButton#navButton {"
               "  border: none;"
               "  border-radius: %1px;"
               "  background-color: transparent;"
               "  icon-size: %2px;"
               "  color: %8;"
               "  font-size: %9px;"
               "  padding-left: %10px;"
               "  padding-right: %10px;"
               "}"
               // On the tinted rail the hover lifts to the white surface so
               // the affordance reads against the rail tone. Placeholders
               // stay contiguous: QString::arg fills by placeholder number,
               // not call order, so a gap would shift every later value.
               "QToolButton#navButton:hover {"
               "  background-color: %3;"
               "  color: %5;"
               "}"
               "QToolButton#navButton:pressed {"
               "  background-color: %4;"
               "}"
               "QToolButton#navButton:checked {"
               "  background-color: %5;"
               "  color: %11;"
               "}"
               "QToolButton#navButton:focus {"
               "  border: 2px solid %6;"
               "}"
               "QToolButton#navButton:checked:focus {"
               "  border: 2px solid %7;"
               "}"
               "QToolButton#navButton:disabled {"
               "  background-color: transparent;"
               "  color: %12;"
               "}")
        .arg(Theme::Radius::md)
        .arg(Theme::Size::navRailIcon)
        .arg(C::surface)  // %3 hover lift on the tinted rail
        .arg(C::pressed)
        .arg(C::primary)
        .arg(C::focus)
        .arg(C::surface)
        .arg(C::text)           // %8 resting label colour
        .arg(Theme::Font::sm)   // %9 label font size
        .arg(Theme::Space::sm)  // %10 side padding
        .arg(C::surface)        // %11 checked label (teal pill → white text)
        .arg(C::textDisabled);  // %12 disabled label
}

// -- Bottom operational status bar (shell status projection) ---------------

QString statusBarQss() {
    namespace C = Theme::Color;
    return QStringLiteral(
               // The band sits one neutral step below the page background
               // (surfaceAlt) so it reads as a distinct full-width
               // operational strip, with a 2px semantic top boundary and a
               // 1px highlight hairline underneath for the classic recessed
               // status-bar groove. Qt QSS has no reliable shadow support,
               // so the groove replaces any box-shadow treatment.
               "QWidget#shellStatusBar {"
               "  background-color: %16;"
               "  border-top: 2px solid %2;"
               "}"
               "QLabel#statusBarTopHighlight {"
               "  background-color: %1;"
               "}"
               "QLabel#statusBarCaption {"
               "  color: %5;"
               "  font-size: %4px;"
               "  background: transparent;"
               "}"
               "QLabel#statusBarModelValue {"
               "  color: %3;"
               "  font-size: %13px;"
               "  background: transparent;"
               "}"
               "QLabel#statusBarMuted {"
               "  color: %5;"
               "  font-size: %4px;"
               "  background: transparent;"
               "}"
               "QLabel#statusBarSeparator {"
               "  background-color: %14;"
               "}"
               "QLabel#statusChipDot {"
               "  background-color: %7;"
               "  border-radius: %8px;"
               "}"
               "QLabel#statusChipDot[chipState=\"ready\"] {"
               "  background-color: %9;"
               "}"
               "QLabel#statusChipDot[chipState=\"loading\"],"
               "QLabel#statusChipDot[chipState=\"translating\"] {"
               "  background-color: %10;"
               "}"
               "QLabel#statusChipDot[chipState=\"paused\"] {"
               "  background-color: %12;"
               "}"
               "QLabel#statusChipDot[chipState=\"downloading\"] {"
               "  background-color: %11;"
               "}"
               "QLabel#statusChipDot[chipState=\"failed\"] {"
               "  background-color: %6;"
               "}"
               "QLabel#statusChipText {"
               "  color: %3;"
               "  font-size: %13px;"
               "  font-weight: bold;"
               "  background: transparent;"
               "}"
               "QProgressBar#statusBarProgress {"
               "  min-height: 6px;"
               "  max-height: 6px;"
               "  background-color: %15;"
               "  border: 1px solid %14;"
               "  border-radius: 3px;"
               "  text-align: center;"
               "}"
               "QProgressBar#statusBarProgress::chunk {"
               "  background-color: %10;"
               "  border-radius: 3px;"
               "}")
        .arg(C::surface)         // %1
        .arg(C::border)          // %2
        .arg(C::text)            // %3
        .arg(Theme::Font::xs)    // %4
        .arg(C::textMuted)       // %5
        .arg(C::error)           // %6
        .arg(C::textDisabled)    // %7
        .arg(Theme::Radius::xs)  // %8
        .arg(C::success)         // %9
        .arg(C::primary)         // %10
        .arg(C::info)            // %11
        .arg(C::warning)         // %12
        .arg(Theme::Font::sm)    // %13
        .arg(C::borderLight)     // %14
        .arg(C::progressTrack)   // %15
        .arg(C::surfaceAlt);     // %16
}

// -- Nonmodal model-unavailable banner -----------------------------------

QString bannerQss() {
    namespace C = Theme::Color;
    return QStringLiteral(
               "QWidget#modelUnavailableBanner {"
               "  background-color: %1;"
               "  border-bottom: 1px solid %2;"
               "}"
               "QLabel#bannerDot {"
               "  background-color: %3;"
               "  border-radius: %4px;"
               "}"
               "QLabel#bannerTitle {"
               "  color: %5;"
               "  font-size: %6px;"
               "  font-weight: bold;"
               "  background: transparent;"
               "}"
               "QLabel#bannerDetail {"
               "  color: %7;"
               "  font-size: %8px;"
               "  background: transparent;"
               "}")
        .arg(C::surface)
        .arg(C::border)
        .arg(C::warning)
        .arg(Theme::Radius::sm)
        .arg(C::text)
        .arg(Theme::Font::md)
        .arg(C::textMuted)
        .arg(Theme::Font::sm);
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
               "QLabel#popupTitle {"
               "  color: %4;"
               "  font-size: %5px;"
               "  font-weight: bold;"
               "  background: transparent;"
               "}"
               "QLabel#popupSectionLabel {"
               "  color: %6;"
               "  font-size: %7px;"
               "  font-weight: bold;"
               "  background: transparent;"
               "}"
               // ── Bounded source preview box ──
               "QFrame#popupSourceBox {"
               "  background-color: %8;"
               "  border: 1px solid %9;"
               "  border-radius: %10px;"
               "}"
               "QLabel#popupSource {"
               "  color: %4;"
               "  font-size: %11px;"
               "  background: transparent;"
               "}"
               // ── Scrollable translation output ──
               "QPlainTextEdit#popupResult {"
               "  background-color: %8;"
               "  border: 1px solid %9;"
               "  border-radius: %10px;"
               "  padding: 6px 8px;"
               "  color: %4;"
               "  font-size: %11px;"
               "  selection-background-color: %12;"
               "  selection-color: %1;"
               "}"
               "QPlainTextEdit#popupResult:focus {"
               "  border-color: %12;"
               "}"
               // ── Status row (state tinted) ──
               "QLabel#popupStatusDot {"
               "  background-color: %6;"
               "  border-radius: %13px;"
               "}"
               "QLabel#popupStatus {"
               "  color: %6;"
               "  font-size: %7px;"
               "  background: transparent;"
               "}"
               "QWidget#popupStatusRow[popupState=\"translating\"] QLabel#popupStatusDot {"
               "  background-color: %12;"
               "}"
               "QWidget#popupStatusRow[popupState=\"translating\"] QLabel#popupStatus {"
               "  color: %12;"
               "}"
               "QWidget#popupStatusRow[popupState=\"done\"] QLabel#popupStatusDot {"
               "  background-color: %14;"
               "}"
               "QWidget#popupStatusRow[popupState=\"done\"] QLabel#popupStatus {"
               "  color: %14;"
               "}"
               "QWidget#popupStatusRow[popupState=\"error\"] QLabel#popupStatusDot {"
               "  background-color: %15;"
               "}"
               "QWidget#popupStatusRow[popupState=\"error\"] QLabel#popupStatus {"
               "  color: %15;"
               "}"
               // ── Compact text buttons ──
               "QPushButton#popupCopyBtn {"
               "  background-color: transparent;"
               "  border: none;"
               "  color: %12;"
               "  font-size: %7px;"
               "  padding: 0 6px;"
               "}"
               "QPushButton#popupCopyBtn:hover {"
               "  color: %16;"
               "}"
               "QPushButton#popupPinBtn {"
               "  background-color: transparent;"
               "  border: 1px solid transparent;"
               "  color: %6;"
               "  font-size: %7px;"
               "  padding: 2px 6px;"
               "  border-radius: %10px;"
               "}"
               "QPushButton#popupPinBtn:hover {"
               "  background-color: %8;"
               "  color: %4;"
               "}"
               "QPushButton#popupPinBtn:checked {"
               "  background-color: %12;"
               "  color: %1;"
               "}"
               "QPushButton#popupCloseBtn {"
               "  background-color: transparent;"
               "  border: none;"
               "  color: %6;"
               "  font-size: %7px;"
               "  padding: 0 4px;"
               "}"
               "QPushButton#popupCloseBtn:hover {"
               "  color: %4;"
               "}"
               "QPushButton#popupRetryBtn {"
               "  background-color: transparent;"
               "  border: 1px solid %15;"
               "  color: %15;"
               "  font-size: %7px;"
               "  padding: 3px 10px;"
               "  border-radius: %10px;"
               "}"
               "QPushButton#popupRetryBtn:hover {"
               "  background-color: %15;"
               "  color: %1;"
               "}")
        .arg(C::surface)         // %1
        .arg(C::border)          // %2
        .arg(Theme::Radius::md)  // %3
        .arg(C::text)            // %4
        .arg(Theme::Font::md)    // %5
        .arg(C::textMuted)       // %6
        .arg(Theme::Font::xs)    // %7
        .arg(C::hover)           // %8
        .arg(C::borderLight)     // %9
        .arg(Theme::Radius::sm)  // %10
        .arg(Theme::Font::lg)    // %11
        .arg(C::primary)         // %12
        .arg(Theme::Radius::xs)  // %13 dot radius
        .arg(C::success)         // %14
        .arg(C::error)           // %15
        .arg(C::primaryHover);   // %16
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
        .arg(Theme::Radius::md)
        .arg(C::text)
        .arg(C::textMuted)
        .arg(Theme::Font::title);
}

// -- Translate work panes (source / result / back-translate) ---------------

QString workPaneQss() {
    namespace C = Theme::Color;
    return QStringLiteral(
               "QFrame#workPane {"
               "  background-color: %1;"
               "  border: 1px solid %2;"
               "  border-radius: %3px;"
               "}"
               "QLabel#workPaneTitle {"
               "  color: %4;"
               "  font-size: %5px;"
               "  font-weight: bold;"
               "  background: transparent;"
               "}"
               "QLabel#charCount {"
               "  color: %6;"
               "  font-size: %7px;"
               "  background: transparent;"
               "}"
               "QFrame#workPane QPlainTextEdit {"
               "  border: none;"
               "  background: transparent;"
               "  padding: 0;"
               "}"
               "QFrame#workPane QPlainTextEdit:focus {"
               "  border: none;"
               "}"
               // ── Terminal-state strip at the bottom of the result pane ──
               "QWidget#resultStateStrip {"
               "  background: transparent;"
               "  min-height: 18px;"
               "}"
               "QLabel#resultStateDot {"
               "  background-color: %6;"
               "  border-radius: %8px;"
               "}"
               "QLabel#resultStateText {"
               "  color: %6;"
               "  font-size: %7px;"
               "  background: transparent;"
               "}"
               "QWidget#resultStateStrip[stripState=\"translating\"] QLabel#resultStateDot {"
               "  background-color: %9;"
               "}"
               "QWidget#resultStateStrip[stripState=\"translating\"] QLabel#resultStateText {"
               "  color: %9;"
               "}"
               "QWidget#resultStateStrip[stripState=\"completed\"] QLabel#resultStateDot {"
               "  background-color: %10;"
               "}"
               "QWidget#resultStateStrip[stripState=\"completed\"] QLabel#resultStateText {"
               "  color: %10;"
               "}"
               "QWidget#resultStateStrip[stripState=\"preempted\"] QLabel#resultStateDot {"
               "  background-color: %11;"
               "}"
               "QWidget#resultStateStrip[stripState=\"preempted\"] QLabel#resultStateText {"
               "  color: %11;"
               "}"
               "QWidget#resultStateStrip[stripState=\"failed\"] QLabel#resultStateDot {"
               "  background-color: %12;"
               "}"
               "QWidget#resultStateStrip[stripState=\"failed\"] QLabel#resultStateText {"
               "  color: %12;"
               "}")
        .arg(C::surface)         // %1
        .arg(C::border)          // %2
        .arg(Theme::Radius::md)  // %3
        .arg(C::text)            // %4
        .arg(Theme::Font::md)    // %5
        .arg(C::textMuted)       // %6
        .arg(Theme::Font::xs)    // %7
        .arg(Theme::Radius::xs)  // %8 dot radius
        .arg(C::primary)         // %9 translating
        .arg(C::success)         // %10 completed
        .arg(C::warning)         // %11 preempted
        .arg(C::error);          // %12 failed
}

// -- Preferences page controls ----------------------------------------------

QString preferencesQss() {
    namespace C = Theme::Color;
    return QStringLiteral(
               // The Preferences scroll surface: the viewport and its plain
               // content widget otherwise paint the palette Base (white),
               // leaving a large white block below the last section. The
               // selector chain targets exactly the scroll area, its
               // viewport, and the content widget — cards inside stay white.
               "QScrollArea#preferencesScroll {"
               "  background-color: transparent;"
               "}"
               "QScrollArea#preferencesScroll > QWidget > QWidget {"
               "  background-color: transparent;"
               "}"
               "QKeySequenceEdit {"
               "  background-color: %1;"
               "  border: 1px solid %2;"
               "  border-radius: %3px;"
               "  padding: 6px 10px;"
               "  min-height: 24px;"
               "  color: %4;"
               "  font-size: %5px;"
               "}"
               "QKeySequenceEdit:focus {"
               "  border-color: %6;"
               "}"
               "QKeySequenceEdit:disabled {"
               "  color: %7;"
               "  background-color: %8;"
               "}"
               "QKeySequenceEdit::edit {"
               "  border: none;"
               "  background: transparent;"
               "}"
               "QLabel#settingsFeedback {"
               "  color: %9;"
               "  font-size: %10px;"
               "  background: transparent;"
               "}"
               "QLabel#settingsFeedback[level=\"error\"] {"
               "  color: %11;"
               "}"
               "QLabel#settingsFeedback[level=\"success\"] {"
               "  color: %12;"
               "}")
        .arg(C::surface)         // %1
        .arg(C::border)          // %2
        .arg(Theme::Radius::sm)  // %3
        .arg(C::text)            // %4
        .arg(Theme::Font::md)    // %5
        .arg(C::focus)           // %6
        .arg(C::textDisabled)    // %7
        .arg(C::hover)           // %8
        .arg(C::textMuted)       // %9
        .arg(Theme::Font::xs)    // %10
        .arg(C::error)           // %11
        .arg(C::success);        // %12
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
               "QLabel#mutedLabel {"
               "  color: %5;"
               "  font-size: %6px;"
               "  background: transparent;"
               "}"
               "QLabel#toolbarCaption {"
               "  color: %5;"
               "  font-size: %6px;"
               "  background: transparent;"
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
               // No QSS padding here: on a plain container QWidget it does
               // not affect layout geometry. The Translate toolbar's
               // horizontal inset lives in its explicit layout margins
               // (translate_page.cpp), matching the Documents language bar.
               "QWidget#translateToolbar {"
               "  background-color: %10;"
               "  border: 1px solid %4;"
               "  border-radius: %11px;"
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
               "  font-size: %15px;"
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
               // A hairline under the in-card section title gives each
               // settings block a clear header boundary instead of a flat
               // field of white.
               "QFrame#settingsSection > QLabel#sectionTitle {"
               "  border-bottom: 1px solid %21;"
               "  padding-bottom: %7;"
               "  margin-bottom: %7;"
               "}"

               "/* ── Preferences page header ── */"
               "QWidget#preferencesHeader {"
               "  background-color: transparent;"
               "  border-bottom: 1px solid %4;"
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
               "  font-size: %13px;"
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
               "}"

               "/* ── Batch work queue table ── */"
               "QTableView#batchQueueTable {"
               "  background-color: %10;"
               "  alternate-background-color: %20;"
               "  border: 1px solid %4;"
               "  border-radius: %11px;"
               "  gridline-color: %21;"
               "  selection-background-color: %12;"
               "  selection-color: %10;"
               "  outline: none;"
               "}"
               "QTableView#batchQueueTable::item {"
               "  padding: 4px 10px;"
               "  border: none;"
               "}"
               "QTableView#batchQueueTable::item:selected {"
               "  background-color: %12;"
               "  color: %10;"
               "}"
               "QHeaderView {"
               "  background-color: %10;"
               "}"
               "QHeaderView::section {"
               "  background-color: %10;"
               "  border: none;"
               "  border-bottom: 1px solid %4;"
               "  border-right: 1px solid %21;"
               "  padding: 6px 10px;"
               "  color: %5;"
               "  font-size: %6px;"
               "  font-weight: bold;"
               "}"
               "QTableCornerButton::section {"
               "  background-color: %10;"
               "  border: none;"
               "  border-bottom: 1px solid %4;"
               "}"

               "/* ── Batch page ── */"
               "QWidget#batchLangBar {"
               "  background-color: %10;"
               "  border: 1px solid %4;"
               "  border-radius: %11px;"
               "}"
               "QLabel#batchEmptyTitle {"
               "  font-size: %8px;"
               "  font-weight: bold;"
               "  color: %9;"
               "  background: transparent;"
               "}"
               "QLabel#batchEmptyHint {"
               "  font-size: %6px;"
               "  color: %5;"
               "  background: transparent;"
               "}"
               "QLabel#batchSummary {"
               "  color: %5;"
               "  font-size: %6px;"
               "  background: transparent;"
               "}"
               "QProgressBar#batchOverallProgress {"
               "  min-height: 6px;"
               "  max-height: 6px;"
               "  background-color: %19;"
               "  border: 1px solid %4;"
               "  border-radius: 3px;"
               "  text-align: center;"
               "}"
               "QProgressBar#batchOverallProgress::chunk {"
               "  background-color: %12;"
               "  border-radius: 3px;"
               "}"

               "/* ── Model library ── */"
               "QFrame#modelRow {"
               "  background-color: %10;"
               "  border: 1px solid %4;"
               "  border-radius: %11px;"
               "}"
               "QFrame#modelRow:hover {"
               "  border-color: %12;"
               "}"
               "QFrame#modelRow[modelRowState=\"configured\"] {"
               "  border-color: %12;"
               "  background-color: %22;"
               "}"
               "QLabel#modelRowName {"
               "  font-size: %13px;"
               "  font-weight: bold;"
               "  color: %9;"
               "  background: transparent;"
               "}"
               "QLabel#modelRowFile {"
               "  font-size: %6px;"
               "  color: %5;"
               "  background: transparent;"
               "}"
               "QLabel#modelRowStatus {"
               "  font-size: %6px;"
               "  color: %5;"
               "  background: transparent;"
               "}"
               "QLabel#configuredBadge {"
               "  background-color: %12;"
               "  color: %10;"
               "  border-radius: %7px;"
               "  padding: 1px 8px;"
               "  font-size: %6px;"
               "  font-weight: bold;"
               "}"
               "QLabel#modelSummaryName {"
               "  font-size: %8px;"
               "  font-weight: bold;"
               "  color: %9;"
               "  background: transparent;"
               "}"
               "QLabel#modelSummaryStatus {"
               "  font-size: %6px;"
               "  color: %5;"
               "  background: transparent;"
               "}"
               "QLabel#modelStatusDot {"
               "  background-color: %19;"
               "  border-radius: %7px;"
               "}"
               "QLabel#modelStatusDot[dotState=\"success\"] {"
               "  background-color: %16;"
               "}"
               "QLabel#modelStatusDot[dotState=\"primary\"] {"
               "  background-color: %12;"
               "}"
               "QLabel#modelStatusDot[dotState=\"warning\"] {"
               "  background-color: %17;"
               "}"
               "QLabel#modelStatusDot[dotState=\"error\"] {"
               "  background-color: %18;"
               "}"
               "QWidget#modelRowsContainer {"
               "  background-color: transparent;"
               "}"
               "QProgressBar#rowProgress {"
               "  min-height: 6px;"
               "  max-height: 6px;"
               "  background-color: %19;"
               "  border: 1px solid %4;"
               "  border-radius: 3px;"
               "  text-align: center;"
               "}"
               "QProgressBar#rowProgress::chunk {"
               "  background-color: %12;"
               "  border-radius: 3px;"
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
               .arg(Theme::Font::xl)  // %13 model row name
               .arg(Theme::Font::md)  // %15 form label
               .arg(C::success)       // %16 status dot success
               .arg(C::warning)       // %17 status dot warning
               .arg(C::error)         // %18 status dot error
               .arg(C::textDisabled)  // %19 status dot muted / progress track
               .arg(C::hover)         // %20 alternating table row
               .arg(C::borderLight)   // %21 table gridline
               .arg(C::successSoft)   // %22 configured row tint
           // Sub-stylesheets — order does not matter for the final cascade.
           + buttonQss() + comboBoxQss() + menuQss() + inputQss() + checkboxQss() + spinBoxQss() + progressBarQss() + scrollBarQss() + splitterQss() + navButtonQss() + statusBarQss() + bannerQss() + workPaneQss() + preferencesQss();
}

QString modalOverlayStyleSheet() {
    return QStringLiteral("#modalOverlay { background-color: %1; }")
        .arg(Theme::Color::overlay);
}

QString modalPanelStyleSheet() {
    return modalPanelQss() + buttonQss() + comboBoxQss() + menuQss();
}

QString popupWindowStyleSheet() {
    // The result editor scrolls, so the themed scrollbars ride along with
    // the popup's own stylesheet context (the app sheet is not applied here).
    return popupFrameQss() + scrollBarQss();
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
