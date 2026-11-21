// File: tests/test_utf8.cpp
// Purpose: UTF-8 scanning at run time: every length of valid sequence and its edges, and every kind of bad one, with
//          the length the Unicode Standard gives its maximal subpart. test_constexpr.cpp proves the same at compile time.
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "check.hpp"
#include "jString/v001/encoding.hpp"

namespace {

using jstr::EncodingError;
using jstr::Scan;
using jstr::Utf8;

constexpr char32_t bad = jstr::replacement_character;

[[nodiscard]] Scan scan(std::string_view units) { return Utf8::scan(units); }

// A code point's UTF-8, written out by the rule in the Unicode Standard's table 3-6, into four bytes: how many it took.
[[nodiscard]] std::size_t encode(char32_t code_point, std::span<char, 4> out) {
    const auto unit = [](std::uint32_t bits) { return static_cast<char>(static_cast<unsigned char>(bits)); };
    const std::uint32_t c = code_point;
    if (c < 0x80U) {
        out[0] = unit(c);
        return 1;
    }
    if (c < 0x800U) {
        out[0] = unit(0xC0U | (c >> 6U));
        out[1] = unit(0x80U | (c & 0x3FU));
        return 2;
    }
    if (c < 0x10000U) {
        out[0] = unit(0xE0U | (c >> 12U));
        out[1] = unit(0x80U | ((c >> 6U) & 0x3FU));
        out[2] = unit(0x80U | (c & 0x3FU));
        return 3;
    }
    out[0] = unit(0xF0U | (c >> 18U));
    out[1] = unit(0x80U | ((c >> 12U) & 0x3FU));
    out[2] = unit(0x80U | ((c >> 6U) & 0x3FU));
    out[3] = unit(0x80U | (c & 0x3FU));
    return 4;
}

}  // namespace

int main() {
    jstr_test::Checker check;

    // Valid, at each length's lowest and highest code point.
    check(scan("A") == Scan{U'A', 1, true}, "one byte");
    check(scan(std::string_view{"\x00", 1}) == Scan{U'\0', 1, true}, "a zero is a valid code point");
    check(scan("\x7F") == Scan{U'\x7F', 1, true}, "highest one-byte");
    check(scan("\xC2\x80") == Scan{U'\u0080', 2, true}, "lowest two-byte");
    check(scan("\xDF\xBF") == Scan{U'߿', 2, true}, "highest two-byte");
    check(scan("\xE0\xA0\x80") == Scan{U'ࠀ', 3, true}, "lowest three-byte");
    check(scan("\xED\x9F\xBF") == Scan{U'퟿', 3, true}, "just below the surrogates");
    check(scan("\xEE\x80\x80") == Scan{U'', 3, true}, "just above the surrogates");
    check(scan("\xEF\xBF\xBF") == Scan{U'￿', 3, true}, "highest three-byte");
    check(scan("\xF0\x90\x80\x80") == Scan{U'\U00010000', 4, true}, "lowest four-byte");
    check(scan("\xF4\x8F\xBF\xBF") == Scan{U'\U0010FFFF', 4, true}, "highest code point");
    check(scan("\xC3\xA9xyz") == Scan{U'é', 2, true}, "only the first code point is read");

    // Bad lead bytes: one byte each.
    check(scan("\x80") == Scan{bad, 1, false}, "a lone continuation byte");
    check(scan("\xBF") == Scan{bad, 1, false}, "the highest continuation byte, alone");
    check(scan("\xC0\x80") == Scan{bad, 1, false}, "C0 is never a lead: overlong");
    check(scan("\xC1\xBF") == Scan{bad, 1, false}, "C1 is never a lead: overlong");
    check(scan("\xF5\x80\x80\x80") == Scan{bad, 1, false}, "F5 would be past U+10FFFF");
    check(scan("\xFF") == Scan{bad, 1, false}, "FF is never UTF-8");

    // Bad second bytes, where the lead narrows the range: the lead alone is the maximal subpart.
    check(scan("\xE0\x80\x80") == Scan{bad, 1, false}, "E0 80: overlong three-byte");
    check(scan("\xE0\x9F\xBF") == Scan{bad, 1, false}, "E0 9F: overlong three-byte");
    check(scan("\xED\xA0\x80") == Scan{bad, 1, false}, "ED A0: a surrogate, U+D800");
    check(scan("\xED\xBF\xBF") == Scan{bad, 1, false}, "ED BF: a surrogate, U+DFFF");
    check(scan("\xF0\x80\x80\x80") == Scan{bad, 1, false}, "F0 80: overlong four-byte");
    check(scan("\xF0\x8F\xBF\xBF") == Scan{bad, 1, false}, "F0 8F: overlong four-byte");
    check(scan("\xF4\x90\x80\x80") == Scan{bad, 1, false}, "F4 90: past U+10FFFF");

    // Cut short, or broken after a good start: the good start is the maximal subpart.
    check(scan("\xC3") == Scan{bad, 1, false}, "a two-byte lead at the end");
    check(scan("\xC3" "A") == Scan{bad, 1, false}, "a two-byte lead before ASCII");
    check(scan("\xE1\x80") == Scan{bad, 2, false}, "a three-byte sequence cut after two");
    check(scan("\xE1\x80" "A") == Scan{bad, 2, false}, "a three-byte sequence broken at the third");
    check(scan("\xF1\x80\x80") == Scan{bad, 3, false}, "a four-byte sequence cut after three");
    check(scan("\xF1\x80\x80\xC0") == Scan{bad, 3, false}, "a four-byte sequence broken at the fourth");

    // The first bad sequence in longer text.
    check(jstr::first_invalid<Utf8>("") == std::nullopt, "empty text is valid");
    check(jstr::first_invalid<Utf8>("plain ASCII") == std::nullopt, "ASCII is valid");
    check(jstr::first_invalid<Utf8>("caf\xC3\xA9 \xF0\x9F\x98\x80") == std::nullopt, "mixed lengths are valid");
    check(jstr::first_invalid<Utf8>("ab\x80" "cd") == EncodingError{2}, "found at its offset");
    check(jstr::first_invalid<Utf8>("\xC3\xA9\xC3") == EncodingError{2}, "found after a valid two-byte sequence");
    check(jstr::first_invalid<Utf8>("\xED\xA0\x80\x41") == EncodingError{0}, "a surrogate at the start");

    // decode() and length_of(), which trust the text, against scan(), which checks it: every code point there is.
    std::size_t disagree = 0;
    for (std::uint32_t c = 0; c <= 0x10FFFFU; ++c) {
        if (c >= 0xD800U && c <= 0xDFFFU) continue;  // surrogates are not code points UTF-8 may hold
        std::array<char, 4> bytes{};
        const std::size_t length = encode(static_cast<char32_t>(c), bytes);
        const std::string_view units{std::span{bytes}.first(length)};
        const Scan checked = scan(units);
        if (!checked.valid || checked.length != length || checked.code_point != static_cast<char32_t>(c) ||
            Utf8::decode(units) != static_cast<char32_t>(c) || Utf8::length_of(units.front()) != length) {
            ++disagree;
        }
    }
    check(disagree == 0, "decode, length_of and scan agree on all 1,112,064 code points");

    // U+FFFD's own bytes are valid UTF-8, and say U+FFFD.
    check(scan(Utf8::replacement()) == Scan{bad, 3, true}, "the replacement is U+FFFD, valid");
    return check.finish();
}
