#pragma once

#include <QString>

// ── Design Tokens ─────────────────────────────────────────────────────
// All colours, spacing, radii, font sizes, and widget dimensions that
// define the visual language of QTrans.  Every stylesheet in the
// application derives from these constants so that a single edit here
// propagates everywhere.
// ──────────────────────────────────────────────────────────────────────

namespace Theme {

// ── Colour Palette ────────────────────────────────────────────────────
// Neutral light surfaces with a teal operational accent; semantic
// colours (success / warning / error / info) cover status projections.
// ──────────────────────────────────────────────────────────────────────
namespace Color {
inline constexpr auto bg = "#f5f5f7";
inline constexpr auto surface = "#ffffff";
// Navigation rail + footer-band neutral: one step below the page background
// so the sidebar and bottom band read as deliberate chrome framing the
// workbench, distinct from both the page and its white cards.
inline constexpr auto sidebar = "#ececf1";
inline constexpr auto border = "#d1d1d6";
inline constexpr auto borderLight = "#e5e5ea";
inline constexpr auto primary = "#0d9488";  // teal operational accent
inline constexpr auto primaryHover = "#0f766e";
inline constexpr auto primaryPressed = "#115e59";
inline constexpr auto text = "#1d1d1f";
inline constexpr auto textMuted = "#6e6e73";
inline constexpr auto textDisabled = "#aeaeb2";
inline constexpr auto danger = "#dc2626";  // legacy alias of error
inline constexpr auto dangerHover = "#b91c1c";
inline constexpr auto overlay = "rgba(60, 60, 67, 0.28)";
inline constexpr auto hover = "#f2f2f7";
inline constexpr auto pressed = "#e5e5ea";
// Footer-band neutral: a step deeper than the page background so the
// bottom operational band reads as a distinct surface, not a page card.
inline constexpr auto surfaceAlt = "#ececf1";
inline constexpr auto progressTrack = "#e5e5ea";
inline constexpr auto selection = "#0d9488";
inline constexpr auto focus = "#0d9488";

// ── Semantic status colours ───────────────────────────────────────────
inline constexpr auto success = "#16a34a";
inline constexpr auto successSoft = "#dcfce7";
inline constexpr auto warning = "#d97706";
inline constexpr auto warningSoft = "#fef3c7";
inline constexpr auto error = "#dc2626";
inline constexpr auto errorSoft = "#fee2e2";
inline constexpr auto info = "#2563eb";
inline constexpr auto infoSoft = "#dbeafe";
}  // namespace Color

// ── Border Radius ─────────────────────────────────────────────────────
// Compact desktop target: 6-8px on control surfaces (sm/md); xs is for
// micro elements (checkbox indicators, scrollbar handles).
namespace Radius {
inline constexpr int xs = 4;
inline constexpr int sm = 6;
inline constexpr int md = 8;
inline constexpr int lg = 8;  // largest panels converge on the md radius
}  // namespace Radius

// ── Spacing ───────────────────────────────────────────────────────────
namespace Space {
inline constexpr int xs = 4;
inline constexpr int sm = 8;
inline constexpr int md = 12;
inline constexpr int lg = 16;
inline constexpr int xl = 20;
inline constexpr int xxl = 24;
inline constexpr int xxxl = 32;
}  // namespace Space

// ── Font Sizes ────────────────────────────────────────────────────────
namespace Font {
inline constexpr int xs = 11;
inline constexpr int sm = 12;
inline constexpr int md = 13;
inline constexpr int lg = 14;
inline constexpr int xl = 15;
inline constexpr int title = 18;
}  // namespace Font

// ── Widget Dimensions ─────────────────────────────────────────────────
namespace Size {
inline constexpr int sidebarWidth = 72;     // icon navigation rail
inline constexpr int statusBarHeight = 32;  // bottom operational status bar
inline constexpr int navItemHeight = 40;
inline constexpr int navRailIcon = 20;
inline constexpr int statusDot = 8;
inline constexpr int minPanelWidth = 220;
// Stable minimum height of the Translate language toolbar so its controls
// never look vertically squeezed at compact window heights.
inline constexpr int toolbarHeight = 56;
// Bottom status bar geometry: the activity chip occupies a fixed-width slot
// so status text changes never shift the model/backend groups; the loaded
// model is a bounded slot (elides at both ends of the range); the backend
// value keeps a fixed width so long labels elide (with a tooltip) instead
// of shifting the layout; download progress and speed/ETA stay compact and
// right-anchored.
inline constexpr int statusBarActivityWidth = 128;
inline constexpr int statusBarLoadedMinWidth = 56;
inline constexpr int statusBarLoadedMaxWidth = 240;
inline constexpr int statusBarBackendWidth = 150;
inline constexpr int statusBarProgressWidth = 130;
inline constexpr int statusBarMetricsMaxWidth = 140;
inline constexpr int statusBarSeparatorHeight = 14;
}  // namespace Size

// ── Sidebar Nav Item Icons (plain char* to keep QStringLiteral out of
//    constexpr context — QString is not a literal type in C++17) ────────
namespace NavIcon {
inline constexpr const char *translate = "\u21C4";
inline constexpr const char *wordSelect = "\u2318";
inline constexpr const char *model = "\u2699";
inline constexpr const char *batch = "\u2601";  // cloud icon for batch
}  // namespace NavIcon

}  // namespace Theme
