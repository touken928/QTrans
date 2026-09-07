#include "sentbreak/segmenter.h"

namespace sentbreak {
namespace {

enum class Property { Other,
                      CR,
                      LF,
                      Sep,
                      Extend,
                      Format,
                      Sp,
                      Lower,
                      Upper,
                      OLetter,
                      ATerm,
                      STerm,
                      Close };

struct CodePoint {
    char32_t value;
    std::size_t begin;
    std::size_t end;
    Property property;
};

bool in_range(char32_t value, char32_t first, char32_t last) {
    return value >= first && value <= last;
}

Property property_of(char32_t value) {
    if (value == U'\r') return Property::CR;
    if (value == U'\n') return Property::LF;
    if (value == 0x0b || value == 0x0c || value == 0x85 ||
        in_range(value, 0x2028, 0x2029)) return Property::Sep;
    if (value == U' ') return Property::Sp;
    if (value == U'.' || value == 0x2024 || value == 0xfe52 ||
        value == 0xff0e) return Property::ATerm;
    if (value == U'!' || value == U'?' || value == 0x3002 ||
        value == 0xff01 || value == 0xff1f) return Property::STerm;
    if (value == U')' || value == U']' || value == U'}' || value == U'\'' ||
        value == U'"' || value == 0xbb || value == 0x2019 || value == 0x201d)
        return Property::Close;
    if (value == 0x200c || value == 0x200d ||
        in_range(value, 0x300, 0x36f) || in_range(value, 0x1ab0, 0x1aff) ||
        in_range(value, 0x1dc0, 0x1dff) || in_range(value, 0x20d0, 0x20ff) ||
        in_range(value, 0xfe00, 0xfe0f)) return Property::Extend;
    if (value == 0x00ad || value == 0x061c || in_range(value, 0x200e, 0x200f) ||
        in_range(value, 0x202a, 0x202e) || in_range(value, 0x2060, 0x2064) ||
        in_range(value, 0xfeff, 0xfeff)) return Property::Format;
    if (in_range(value, U'a', U'z') || in_range(value, 0x00e0, 0x00f6) ||
        in_range(value, 0x00f8, 0x00ff)) return Property::Lower;
    if (in_range(value, U'A', U'Z') || in_range(value, 0x00c0, 0x00d6) ||
        in_range(value, 0x00d8, 0x00de)) return Property::Upper;
    if (in_range(value, 0x0370, 0x052f) || in_range(value, 0x0531, 0x1fff) ||
        in_range(value, 0x3040, 0x30ff) || in_range(value, 0x3400, 0x9fff) ||
        in_range(value, 0xac00, 0xd7af) || in_range(value, 0xf900, 0xfaff) ||
        in_range(value, 0x10000, 0x10ffff)) return Property::OLetter;
    return Property::Other;
}

bool is_term(Property property) {
    return property == Property::ATerm || property == Property::STerm;
}

bool is_letter(Property property) {
    return property == Property::Lower || property == Property::Upper ||
           property == Property::OLetter;
}

std::size_t previous_word_length(const std::vector<CodePoint> &points,
                                 std::size_t term_index) {
    std::size_t length = 0;
    for (std::size_t cursor = term_index; cursor > 0;) {
        --cursor;
        if (!is_letter(points[cursor].property)) break;
        ++length;
    }
    return length;
}

std::vector<CodePoint> decode(std::string_view text) {
    std::vector<CodePoint> result;
    for (std::size_t i = 0; i < text.size();) {
        const std::size_t begin = i;
        unsigned char first = static_cast<unsigned char>(text[i++]);
        char32_t value = first;
        std::size_t length = 1;
        if (first >= 0xc2 && first <= 0xdf)
            length = 2;
        else if (first >= 0xe0 && first <= 0xef)
            length = 3;
        else if (first >= 0xf0 && first <= 0xf4)
            length = 4;
        if (length > 1 && begin + length <= text.size()) {
            value = first & ((1u << (8 - length - 1)) - 1);
            bool valid = true;
            for (std::size_t j = 1; j < length; ++j) {
                const auto byte = static_cast<unsigned char>(text[begin + j]);
                if ((byte & 0xc0) != 0x80) valid = false;
                value = (value << 6) | (byte & 0x3f);
            }
            valid = valid && value <= 0x10ffff &&
                    !(value >= 0xd800 && value <= 0xdfff);
            if (valid)
                i = begin + length;
            else
                value = first;
        } else {
            value = first;
        }
        result.push_back({value, begin, i, property_of(value)});
    }
    return result;
}

bool break_at(const std::vector<CodePoint> &points, std::size_t index) {
    const Property left = points[index - 1].property;
    const Property right = points[index].property;
    if (left == Property::CR && right == Property::LF) return false;
    if (left == Property::CR || left == Property::LF || left == Property::Sep)
        return true;
    if (right == Property::Extend || right == Property::Format) return false;

    std::size_t cursor = index - 1;
    while (cursor > 0 &&
           (points[cursor].property == Property::Sp ||
            points[cursor].property == Property::Close ||
            points[cursor].property == Property::Extend ||
            points[cursor].property == Property::Format))
        --cursor;
    if (is_term(points[cursor].property)) {
        if (!is_letter(right)) return false;
        const Property term = points[cursor].property;
        if (term == Property::ATerm && previous_word_length(points, cursor) <= 2)
            return false;
        if (term == Property::ATerm && right == Property::Lower) return false;
        return true;
    }
    return false;
}

}  // namespace

Segmenter::Segmenter(Algorithm algorithm)
    : algorithm_(algorithm) {
}

Segmenter Segmenter::uax29() {
    return Segmenter(Algorithm::Uax29);
}

std::vector<std::size_t> Segmenter::boundaries(std::string_view text) const {
    const auto points = decode(text);
    std::vector<std::size_t> result{0};
    for (std::size_t i = 1; i < points.size(); ++i) {
        if (break_at(points, i)) result.push_back(points[i].begin);
    }
    if (result.back() != text.size()) result.push_back(text.size());
    return result;
}

std::vector<std::string_view> Segmenter::split(std::string_view text) const {
    const auto positions = boundaries(text);
    std::vector<std::string_view> result;
    for (std::size_t i = 1; i < positions.size(); ++i) {
        if (positions[i] != positions[i - 1])
            result.emplace_back(text.data() + positions[i - 1],
                                positions[i] - positions[i - 1]);
    }
    return result;
}

}  // namespace sentbreak
