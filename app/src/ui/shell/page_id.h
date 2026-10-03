#pragma once

#include <QtGlobal>

// Typed identity for top-level shell pages. Replaces fragile hardcoded
// stack indexes: pages are addressed by name everywhere in the shell, and
// the mapping to the QStackedWidget index lives in exactly one place
// (stackIndexOfPage below). Word Select is not a top-level page — its
// settings live inside the Preferences page — so page identity never
// shifts when shell pages move.
enum class PageId {
    Translate = 0,
    Documents,  // batch translation
    Models,
    Preferences,  // settings sections (General / Word Select / API / Advanced)
    Count,
};

// The single source of truth for the QStackedWidget ordering. Invalid or
// Count values must never silently map onto a real page: debug builds
// assert, and release builds fail safe with -1 so the caller does nothing
// instead of selecting the wrong page.
inline int stackIndexOfPage(PageId page) {
    switch (page) {
        case PageId::Translate:
            return 0;
        case PageId::Documents:
            return 1;
        case PageId::Models:
            return 2;
        case PageId::Preferences:
            return 3;
        case PageId::Count:
            break;
    }
    Q_ASSERT_X(false, "stackIndexOfPage", "invalid PageId value");
    return -1;
}
