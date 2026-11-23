// File: tests/test_codecs.cpp
// Purpose: The three encodings besides UTF-8 (test_utf8.cpp has that one): ASCII, UTF-16 and UTF-32. scan() on what is
//          valid and on every kind of bad unit, and scan, decode, encode and length_of agreeing on every code point.
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "check.hpp"
#include "jString/v001/encoding.hpp"

namespace {

using jString::Ascii;
using jString::EncodingError;
using jString::Scan;
using jString::Utf16;
using jString::Utf32;

constexpr char32_t bad = jString::replacement_character;

// Whether scan, decode, encode and length_of agree on code_point, in encoding E.
template <class E>
[[nodiscard]] bool agrees(char32_t code_point) {
    std::array<typename E::unit, E::max_units> units{};
    const std::size_t length = E::encode(code_point, units);
    const std::basic_string_view<typename E::unit> written{units.data(), length};
    const Scan scan = E::scan(written);
    return scan.valid && scan.length == length && scan.code_point == code_point && E::decode(std::span{units}.first(length)) == code_point &&
           E::length_of(units[0]) == length && E::starts_code_point(units[0]);
}

}  // namespace

int main() {
    jstr_test::Checker check;

    // --- ASCII: below 80 is a code point, anything else is one bad byte ---
    check(Ascii::scan("A") == Scan{U'A', 1, true} && Ascii::scan("\x7F") == Scan{U'\x7F', 1, true}, "below 80");
    check(Ascii::scan("\x80") == Scan{bad, 1, false} && Ascii::scan("\xC3\xA9") == Scan{bad, 1, false}, "80 and above: one bad byte each");
    check(jString::first_invalid<Ascii>("caf\xC3\xA9") == EncodingError{3}, "found at its offset");
    std::array<char, 1> one{};
    check(Ascii::encode(U'z', one) == 1 && one[0] == 'z' && Ascii::encode(U'é', one) == 1 && one[0] == '?', "encode: the code point, or '?'");

    // --- UTF-16: pairs above U+FFFF; a surrogate anywhere else is one bad unit ---
    check(Utf16::scan(u"A") == Scan{U'A', 1, true} && Utf16::scan(u"￿") == Scan{U'￿', 1, true}, "one unit");
    check(Utf16::scan(u"\U00010000") == Scan{U'\U00010000', 2, true} && Utf16::scan(u"\U0010FFFF") == Scan{U'\U0010FFFF', 2, true}, "a pair, lowest and highest");
    const std::array<char16_t, 2> high_then_a{u'\xD800', u'A'};
    check(Utf16::scan(std::u16string_view{high_then_a.data(), 2}) == Scan{bad, 1, false}, "a high surrogate with no low one");
    const std::array<char16_t, 1> high_alone{u'\xDBFF'};
    check(Utf16::scan(std::u16string_view{high_alone.data(), 1}) == Scan{bad, 1, false}, "a high surrogate at the end");
    const std::array<char16_t, 1> low_alone{u'\xDC00'};
    check(Utf16::scan(std::u16string_view{low_alone.data(), 1}) == Scan{bad, 1, false}, "a low surrogate first");
    check(!Utf16::starts_code_point(u'\xDC00') && Utf16::starts_code_point(u'\xD800') && Utf16::length_of(u'\xD800') == 2, "where a code point starts");
    check(Utf16::rank(u'\xFFFF') < Utf16::rank(u'\xD800') && Utf16::rank(u'\xD7FF') < Utf16::rank(u'\xE000'), "rank: a pair above every single unit");

    // --- UTF-32: the code point, if it is one ---
    check(Utf32::scan(U"\U0010FFFF") == Scan{U'\U0010FFFF', 1, true}, "the highest code point");
    const std::array<char32_t, 1> surrogate{static_cast<char32_t>(0xD800)};
    const std::array<char32_t, 1> past_the_end{static_cast<char32_t>(0x110000)};
    check(Utf32::scan(std::u32string_view{surrogate.data(), 1}) == Scan{bad, 1, false}, "a surrogate is not one");
    check(Utf32::scan(std::u32string_view{past_the_end.data(), 1}) == Scan{bad, 1, false}, "nor is anything past U+10FFFF");

    // --- Every code point there is, both ways ---
    std::size_t disagree = 0;
    for (std::uint32_t c = 0; c <= 0x10FFFFU; ++c) {
        if (c >= 0xD800U && c <= 0xDFFFU) continue;
        if (!agrees<Utf16>(static_cast<char32_t>(c)) || !agrees<Utf32>(static_cast<char32_t>(c))) ++disagree;
        if (c < 0x80U && !agrees<Ascii>(static_cast<char32_t>(c))) ++disagree;
    }
    check(disagree == 0, "scan, decode, encode and length_of agree on all 1,112,064 code points");
    std::size_t misordered = 0;
    for (std::uint32_t c = 1; c <= 0xFFFFU; ++c) {
        if (Utf16::rank(static_cast<char16_t>(c - 1)) >= Utf16::rank(static_cast<char16_t>(c)) && c != 0xD800U && c != 0xE000U) ++misordered;
    }
    check(misordered == 0, "rank keeps the order of the units within each run");
    return check.finish();
}
