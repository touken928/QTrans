#include "app/ui/app_theme.h"

#include <QWidget>

// =============================================================================
// Shared sub-stylesheets (reused by both applicationStyleSheet and modalPanel)
// =============================================================================

namespace {

// -- Buttons ----------------------------------------------------------------

QString buttonStyleSheet() {
    return QStringLiteral(R"(
        QPushButton {
            background-color: #ffffff;
            border: 1px solid #d1d1d6;
            border-radius: 6px;
            padding: 8px 14px;
            color: #1d1d1f;
        }
        QPushButton:hover {
            background-color: #f2f2f7;
            border-color: #c7c7cc;
        }
        QPushButton:pressed {
            background-color: #e5e5ea;
        }
        QPushButton:disabled {
            color: #aeaeb2;
            background-color: #f2f2f7;
        }
        QPushButton#primaryButton,
        QPushButton#translateButton {
            background-color: #0071e3;
            border-color: #0071e3;
            color: #ffffff;
            font-weight: bold;
        }
        QPushButton#primaryButton:hover,
        QPushButton#translateButton:hover {
            background-color: #0077ed;
            border-color: #0077ed;
        }
        QPushButton#primaryButton:pressed,
        QPushButton#translateButton:pressed {
            background-color: #006edb;
            border-color: #006edb;
            color: #ffffff;
        }
        QPushButton#primaryButton:disabled,
        QPushButton#translateButton:disabled {
            background-color: #aeaeb2;
            border-color: #aeaeb2;
            color: #ffffff;
        }
    )");
}

// -- Drop-down lists --------------------------------------------------------

QString comboBoxStyleSheet() {
    return QStringLiteral(R"(
        QComboBox {
            background-color: #ffffff;
            border: 1px solid #d1d1d6;
            border-radius: 6px;
            padding: 6px 30px 6px 10px;
            min-height: 24px;
            color: #1d1d1f;
        }
        QComboBox:disabled {
            color: #aeaeb2;
            background-color: #f2f2f7;
        }
        QComboBox::drop-down {
            subcontrol-origin: padding;
            subcontrol-position: top right;
            width: 26px;
            border: none;
            border-left: 1px solid #d1d1d6;
            border-top-right-radius: 6px;
            border-bottom-right-radius: 6px;
            background-color: #ffffff;
        }
        QComboBox::down-arrow {
            width: 12px;
            height: 12px;
        }
        QComboBox QAbstractItemView {
            background-color: #ffffff;
            color: #1d1d1f;
            border: 1px solid #d1d1d6;
            border-radius: 6px;
            padding: 4px;
            outline: none;
            selection-background-color: #0071e3;
            selection-color: #ffffff;
        }
        QComboBox QAbstractItemView::item {
            min-height: 30px;
            padding: 6px 12px;
            color: #1d1d1f;
            background-color: #ffffff;
        }
        QComboBox QAbstractItemView::item:selected {
            background-color: #0071e3;
            color: #ffffff;
        }
        QComboBox QAbstractItemView::item:hover {
            background-color: #f2f2f7;
            color: #1d1d1f;
        }
    )");
}

// -- Context menus ---------------------------------------------------------

QString menuStyleSheet() {
    return QStringLiteral(R"(
        QMenu {
            background-color: #ffffff;
            border: 1px solid #d1d1d6;
            border-radius: 6px;
            padding: 4px;
        }
        QMenu::item {
            padding: 6px 24px;
            border-radius: 4px;
        }
        QMenu::item:selected {
            background-color: #0071e3;
            color: #ffffff;
        }
        QMenu::item:disabled {
            color: #aeaeb2;
        }
        QMenu::separator {
            height: 1px;
            background-color: #d1d1d6;
            margin: 4px 8px;
        }
    )");
}

}  // namespace

// =============================================================================
// AppTheme  — public API
// =============================================================================

namespace AppTheme {

// -- Main application stylesheet -------------------------------------------
// Order: root > window > sidebar > form controls > checkboxes > labels >
//        scrollbars > progress > structural widgets > navigation buttons.
// Sub-stylesheets are appended in the same logical order.
// --------------------------------------------------------------------------

QString applicationStyleSheet() {
    return QStringLiteral(R"(
        /* ---- Root defaults ---- */
        QWidget {
            color: #1d1d1f;
        }

        /* ---- Window & page structure ---- */
        QMainWindow, QWidget#centralRoot {
            background-color: #f5f5f7;
        }
        QStackedWidget {
            background-color: transparent;
        }
        QSplitter::handle {
            background-color: transparent;
        }

        /* ---- Sidebar ---- */
        QWidget#sidebar {
            background-color: #ffffff;
            border-right: 1px solid #d1d1d6;
        }
        QLabel#sidebarLogo {
            background: transparent;
            margin: 0;
            padding: 0;
        }
        QPushButton#navButton {
            text-align: left;
            padding: 10px 14px;
            border: none;
            border-radius: 8px;
            background-color: transparent;
        }
        QPushButton#navButton:hover {
            background-color: #f2f2f7;
        }
        QPushButton#navButton:checked {
            background-color: #0071e3;
            color: #ffffff;
            font-weight: bold;
        }

        /* ---- Text inputs ---- */
        QLineEdit, QPlainTextEdit {
            background-color: #ffffff;
            border: 1px solid #d1d1d6;
            border-radius: 6px;
            padding: 8px;
            selection-background-color: #0071e3;
            selection-color: #ffffff;
        }
        QPlainTextEdit {
            font-size: 14px;
        }

        /* ---- Numeric input ---- */
        QSpinBox {
            background-color: #ffffff;
            border: 1px solid #d1d1d6;
            border-radius: 6px;
            padding: 6px 8px;
            min-height: 24px;
        }
        QSpinBox:disabled {
            color: #aeaeb2;
            background-color: #f2f2f7;
        }
        QSpinBox::up-button, QSpinBox::down-button {
            subcontrol-origin: border;
            width: 24px;
            border: none;
        }

        /* ---- Checkboxes ---- */
        QCheckBox {
            spacing: 8px;
        }
        QCheckBox::indicator {
            width: 18px;
            height: 18px;
            border: 1px solid #aeaeb2;
            border-radius: 4px;
            background-color: #ffffff;
        }
        QCheckBox::indicator:checked {
            background-color: #0071e3;
            border-color: #0071e3;
        }
        QCheckBox:disabled {
            color: #aeaeb2;
        }

        /* ---- Labels ---- */
        QLabel#panelLabel,
        QLabel#statusLabel,
        QLabel#modelStatus,
        QLabel#mutedLabel {
            color: #6e6e73;
        }

        /* ---- Progress bars ---- */
        QProgressBar {
            background-color: #e5e5ea;
            border: 1px solid #d1d1d6;
            border-radius: 6px;
            text-align: center;
        }
        QProgressBar::chunk {
            background-color: #0071e3;
            border-radius: 5px;
        }
    )") + buttonStyleSheet() +
           comboBoxStyleSheet() + menuStyleSheet();
}

// -- Modal overlay ---------------------------------------------------------

QString modalOverlayStyleSheet() {
    return QStringLiteral("#modalOverlay { background-color: rgba(60, 60, 67, 0.28); }");
}

// -- Modal dialog panel ----------------------------------------------------

QString modalPanelStyleSheet() {
    return QStringLiteral(R"(
        QFrame#modalPanel {
            background-color: #ffffff;
            border: 1px solid #d1d1d6;
            border-radius: 12px;
            color: #1d1d1f;
        }
        QFrame#modalPanel QLabel {
            background-color: transparent;
            color: #1d1d1f;
        }
        QFrame#modalPanel QLabel#mutedLabel {
            color: #6e6e73;
        }
    )") + buttonStyleSheet() +
           comboBoxStyleSheet() + menuStyleSheet();
}

// -- Apply stylesheet to a widget and its children -------------------------

void apply(QWidget *widget) {
    if (widget != nullptr) {
        widget->setStyleSheet(applicationStyleSheet());
    }
}

}  // namespace AppTheme
