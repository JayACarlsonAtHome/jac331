// File: tests/test_utf8.cpp
// Purpose: UTF-8 scanning at run time: every length of valid sequence and its edges, and every kind of bad one, with
//          the length the Unicode Standard gives its maximal subpart. test_constexpr.cpp proves the same at compile time.
//          validate(), which checks valid text by a DFA and scans only text that is not, against scan on every kind of
//          sequence.
// Modified 2026-11-24 by Claude Opus 5.5: validate()'s DFA against scan.
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "check.hpp"
#include "jString/v001/encoding.hpp"

namespace {

using jString::EncodingError;
using jString::Scan;
using jString::Utf8;

constexpr char32_t bad = jString::replacement_character;

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

// The same code point as jString's own encode writes it.
[[nodiscard]] std::string encoded_by_jstring(std::uint32_t code_point) {
    std::array<char, 4> bytes{};
    const std::size_t length = Utf8::encode(static_cast<char32_t>(code_point), bytes);
    return std::string{bytes.data(), length};
}

// What validate() must give, found by scan alone: the code point count, or where the first bad sequence starts.
[[nodiscard]] std::expected<std::size_t, EncodingError> scanned(std::string_view units) {
    std::size_t at = 0;
    std::size_t count = 0;
    while (at < units.size()) {
        const Scan one = scan(units.substr(at));
        if (!one.valid) return std::unexpected(EncodingError{at});
        at += one.length;
        ++count;
    }
    return count;
}

// Whether validate() agrees with scan on these bytes: alone (stepped through a byte at a time, shorter than a block),
// and with 14 ASCII bytes before them and 16 after (across the edge of a block, then a block of ASCII).
[[nodiscard]] bool validate_agrees(std::string_view bytes) {
    const std::string padded = std::string(14, 'a') + std::string{bytes} + std::string(16, 'b');
    return jString::validate<Utf8>(bytes) == scanned(bytes) && jString::validate<Utf8>(padded) == scanned(padded);
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
    check(jString::first_invalid<Utf8>("") == std::nullopt, "empty text is valid");
    check(jString::first_invalid<Utf8>("plain ASCII") == std::nullopt, "ASCII is valid");
    check(jString::first_invalid<Utf8>("caf\xC3\xA9 \xF0\x9F\x98\x80") == std::nullopt, "mixed lengths are valid");
    check(jString::first_invalid<Utf8>("ab\x80" "cd") == EncodingError{2}, "found at its offset");
    check(jString::first_invalid<Utf8>("\xC3\xA9\xC3") == EncodingError{2}, "found after a valid two-byte sequence");
    check(jString::first_invalid<Utf8>("\xED\xA0\x80\x41") == EncodingError{0}, "a surrogate at the start");

    // decode() and length_of(), which trust the text, against scan(), which checks it: every code point there is.
    std::size_t disagree = 0;
    for (std::uint32_t c = 0; c <= 0x10FFFFU; ++c) {
        if (c >= 0xD800U && c <= 0xDFFFU) continue;  // surrogates are not code points UTF-8 may hold
        std::array<char, 4> bytes{};
        const std::size_t length = encode(static_cast<char32_t>(c), bytes);
        const std::string_view units{std::span{bytes}.first(length)};
        const Scan checked = scan(units);
        if (!checked.valid || checked.length != length || checked.code_point != static_cast<char32_t>(c) ||
            Utf8::decode(units) != static_cast<char32_t>(c) || Utf8::length_of(units.front()) != length || encoded_by_jstring(c) != units) {
            ++disagree;
        }
    }
    check(disagree == 0, "decode, length_of, scan and encode agree on all 1,112,064 code points");

    // U+FFFD's own bytes are valid UTF-8, and say U+FFFD.
    check(scan(Utf8::replacement()) == Scan{bad, 3, true}, "the replacement is U+FFFD, valid");

    // validate() against scan: every byte, and every pair of bytes; then every sequence of up to four of the bytes at
    // the edges of the ranges table 3-7 names (the DFA's rows change only there).
    std::size_t validate_wrong = 0;
    for (std::uint32_t first = 0; first < 256; ++first) {
        const char one = static_cast<char>(static_cast<unsigned char>(first));
        if (!validate_agrees(std::string_view{&one, 1})) ++validate_wrong;
        for (std::uint32_t second = 0; second < 256; ++second) {
            const std::array<char, 2> two{one, static_cast<char>(static_cast<unsigned char>(second))};
            if (!validate_agrees(std::string_view{two.data(), two.size()})) ++validate_wrong;
        }
    }
    check(validate_wrong == 0, "validate agrees with scan on every byte and every two bytes");
    constexpr std::array<std::uint8_t, 24> edges{0x00, 0x7F, 0x80, 0x8F, 0x90, 0x9F, 0xA0, 0xBF, 0xC0, 0xC1, 0xC2, 0xDF,
                                                 0xE0, 0xE1, 0xEC, 0xED, 0xEE, 0xEF, 0xF0, 0xF1, 0xF3, 0xF4, 0xF5, 0xFF};
    validate_wrong = 0;
    for (const std::uint8_t a : edges) {
        for (const std::uint8_t b : edges) {
            for (const std::uint8_t c : edges) {
                const std::array<char, 3> three{static_cast<char>(a), static_cast<char>(b), static_cast<char>(c)};
                if (!validate_agrees(std::string_view{three.data(), three.size()})) ++validate_wrong;
                for (const std::uint8_t d : edges) {
                    const std::array<char, 4> four{static_cast<char>(a), static_cast<char>(b), static_cast<char>(c), static_cast<char>(d)};
                    if (!validate_agrees(std::string_view{four.data(), four.size()})) ++validate_wrong;
                }
            }
        }
    }
    check(validate_wrong == 0, "and on every three and four of the bytes at the edges of the ranges");
    std::string every;
    for (std::uint32_t c = 0; c <= 0x10FFFFU; ++c) {
        if (c < 0xD800U || c > 0xDFFFU) every += encoded_by_jstring(c);
    }
    check(jString::validate<Utf8>(every) == std::expected<std::size_t, EncodingError>{1'112'064U}, "and every code point in one text is valid");
    return check.finish();
}
