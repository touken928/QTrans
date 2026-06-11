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
// (Apple-inspired neutral + #0071e3 blue accent)
// ──────────────────────────────────────────────────────────────────────
namespace Color {
inline constexpr auto bg = "#f5f5f7";
inline constexpr auto surface = "#ffffff";
inline constexpr auto sidebar = "#ffffff";
inline constexpr auto border = "#d1d1d6";
inline constexpr auto borderLight = "#e5e5ea";
inline constexpr auto primary = "#0071e3";
inline constexpr auto primaryHover = "#0077ed";
inline constexpr auto primaryPressed = "#006edb";
inline constexpr auto text = "#1d1d1f";
inline constexpr auto textMuted = "#6e6e73";
inline constexpr auto textDisabled = "#aeaeb2";
inline constexpr auto danger = "#ff3b30";
inline constexpr auto dangerHover = "#ff453a";
inline constexpr auto overlay = "rgba(60, 60, 67, 0.28)";
inline constexpr auto hover = "#f2f2f7";
inline constexpr auto pressed = "#e5e5ea";
inline constexpr auto progressTrack = "#e5e5ea";
inline constexpr auto selection = "#0071e3";
}  // namespace Color

// ── Border Radius ─────────────────────────────────────────────────────
namespace Radius {
inline constexpr int xs = 4;
inline constexpr int sm = 6;
inline constexpr int md = 8;
inline constexpr int lg = 12;
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
inline constexpr int sidebarWidth = 200;
inline constexpr int minPanelWidth = 220;
inline constexpr int navItemHeight = 40;
}  // namespace Size

// ── Sidebar Nav Item Icons (plain char* to keep QStringLiteral out of
//    constexpr context — QString is not a literal type in C++17) ────────
namespace NavIcon {
inline constexpr const char *translate = "\u21C4";
inline constexpr const char *wordSelect = "\u2318";
inline constexpr const char *model = "\u2699";
}  // namespace NavIcon

}  // namespace Theme
