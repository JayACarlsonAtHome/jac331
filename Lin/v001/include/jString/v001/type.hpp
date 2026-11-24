// File: include/jString/v001/type.hpp
// Purpose: Type, the one choice every jString makes and must name (there is no default): how its text is kept. Then
//          the work on kept text that every Type answers in its own way, none of it for callers to use: counting,
//          finding a code point's place, comparing, searching, and converting from one Type to another.
//
//            Type        unit      a code point takes                 code point n is found by
//            Ascii       char      1 byte (U+0000 to U+007F only)     multiplying
//            UTF8Packed  char      1 to 4 bytes, as files hold it     walking the text
//            UTF8Full    char      4 bytes: its UTF-8, then zeros     multiplying
//            UTF16       char16_t  1 or 2 units                       walking the text
//            UTF32       char32_t  1 unit, the code point itself      multiplying
//
//          UTF16 and UTF32 are kept in this machine's byte order. Every Type compares in code point order on every
//          machine: Ascii, UTF8Packed and UTF8Full as bytes, UTF32 as numbers, UTF16 by rank (see Utf16::rank).
// Modified 2026-11-24 by Claude Opus 5.5: offset_of passes whole blocks counted wide before it walks; UTF-8 written
//          straight from UTF16 and UTF32; decode_at gives UTF-8 the rest of the text.
#pragma once

#include <algorithm>
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>

#include "cells.hpp"
#include "encoding.hpp"

namespace jString::inline v001 {

enum class Type : unsigned char { Ascii, UTF8Packed, UTF8Full, UTF16, UTF32 };

namespace detail {

template <Type T>
struct Layout;

template <>
struct Layout<Type::Ascii> {
    using codec = Ascii;
    static constexpr std::size_t width = 1;
};
template <>
struct Layout<Type::UTF8Packed> {
    using codec = Utf8;
    static constexpr std::size_t width = 0;  // 0: not one width, so a place is found by walking
};
template <>
struct Layout<Type::UTF8Full> {
    using codec = Utf8;
    static constexpr std::size_t width = Utf8::cell_units;
};
template <>
struct Layout<Type::UTF16> {
    using codec = Utf16;
    static constexpr std::size_t width = 0;
};
template <>
struct Layout<Type::UTF32> {
    using codec = Utf32;
    static constexpr std::size_t width = 1;
};

template <Type T>
using codec_of = typename Layout<T>::codec;
template <Type T>
using unit_of = typename codec_of<T>::unit;
template <Type T>
inline constexpr std::size_t width_of = Layout<T>::width;
// The most units one code point is kept in.
template <Type T>
inline constexpr std::size_t max_kept = width_of<T> != 0 ? width_of<T> : codec_of<T>::max_units;
// The Type whose kept text is T's text packed, as files and C libraries hold it: itself, but for UTF8Full.
template <Type T>
inline constexpr Type packed_type = T == Type::UTF8Full ? Type::UTF8Packed : T;
// Both Types keep a code point as the same units, so text goes from one to the other by copying.
template <Type A, Type B>
inline constexpr bool copies_to = A == B || (A == Type::Ascii && B == Type::UTF8Packed);

template <Type T>
using units_view = std::basic_string_view<unit_of<T>>;
template <Type T>
using kept = std::span<const unit_of<T>>;

template <Type T>
[[nodiscard]] constexpr units_view<T> view_of(kept<T> units) noexcept {
    return units_view<T>{units.begin(), units.end()};
}

// A unit as the number it holds: char as unsigned, so that a byte above 7F is above 7F.
template <class Unit>
[[nodiscard]] constexpr std::uint32_t value_of(Unit unit) noexcept {
    if constexpr (std::is_same_v<Unit, char>) {
        return static_cast<unsigned char>(unit);
    } else {
        return static_cast<std::uint32_t>(unit);
    }
}

// How many units the code point at at takes, in valid kept text.
template <Type T>
[[nodiscard]] constexpr std::size_t length_at(kept<T> text, std::size_t at) noexcept {
    if constexpr (width_of<T> != 0) {
        return width_of<T>;
    } else {
        return codec_of<T>::length_of(text[at]);
    }
}

// The code point at at, in valid kept text. UTF-8 is given the rest of the text, not its code point cut out: its decode
// reads only as far as the lead byte says, and the cut is one more length found and checked at every code point.
// (UTF-16, given the rest, was slower on clang++.)
template <Type T>
[[nodiscard]] constexpr char32_t decode_at(kept<T> text, std::size_t at) noexcept {
    if constexpr (std::is_same_v<codec_of<T>, Utf8>) {
        return Utf8::decode(text.subspan(at));
    } else {
        return codec_of<T>::decode(text.subspan(at, length_at<T>(text, at)));
    }
}

// How many units start a code point: a loop with no branch, which compilers make wide (and wider still over a fixed
// size).
template <Type T, std::size_t Size = std::dynamic_extent>
[[nodiscard]] constexpr std::size_t starts_in(std::span<const unit_of<T>, Size> units) noexcept {
    std::size_t count = 0;
    for (const auto unit : units) count += codec_of<T>::starts_code_point(unit) ? 1U : 0U;
    return count;
}

template <Type T>
[[nodiscard]] constexpr std::size_t count_of(kept<T> text) noexcept {
    if constexpr (width_of<T> != 0) {
        return text.size() / width_of<T>;
    } else {
        return starts_in<T>(text);
    }
}

// The units offset_of counts at once in a Type that is walked: enough that the counting is wide, few enough that the
// walk after it is short.
inline constexpr std::size_t offset_block = 64;

// No place: what offset_of gives when the text is too short. A plain number, not a std::optional, because it is on
// the path of every at() and substring(): with -ftrivial-auto-var-init=zero (the hardening every build has), g++
// writes each std::optional to the stack zeroed and reads it back, which made at() over 20 times slower.
inline constexpr std::size_t nowhere = static_cast<std::size_t>(-1);

// Where code point n starts (n may be the count: the end), in units, or nowhere when the text holds fewer. In a Type
// that is walked, whole blocks are passed over first, a block at a time, while the code points that start in a block
// (counted as count_of counts, wide) are no more than those still to pass. A block may end inside a code point, so the
// units that continue it are passed, and the rest, under a block, is walked a code point at a time.
template <Type T>
[[nodiscard]] constexpr std::size_t offset_of(kept<T> text, std::size_t n) noexcept {
    if constexpr (width_of<T> != 0) {
        if (n > text.size() / width_of<T>) return nowhere;
        return n * width_of<T>;
    } else {
        std::size_t at = 0;
        if (text.size() >= offset_block) {
            while (text.size() - at >= offset_block) {
                const std::size_t starts = starts_in<T>(text.subspan(at).template first<offset_block>());
                if (starts > n) break;
                n -= starts;
                at += offset_block;
            }
            while (at < text.size() && !codec_of<T>::starts_code_point(text[at])) ++at;
        }
        for (std::size_t i = 0; i < n; ++i) {
            if (at >= text.size()) return nowhere;
            at += codec_of<T>::length_of(text[at]);
        }
        return at;
    }
}

// A code point's kept units.
template <Type T>
struct Encoded {
    std::array<unit_of<T>, max_kept<T>> units{};
    std::size_t size{};

    [[nodiscard]] constexpr kept<T> view() const noexcept { return kept<T>{units}.first(size); }
};

// code_point kept as T keeps it: in UTF8Full a whole cell, padding and all. In Ascii, '?' for one above U+007F.
template <Type T>
[[nodiscard]] constexpr Encoded<T> encode_kept(char32_t code_point) noexcept {
    Encoded<T> made;
    const std::size_t used = codec_of<T>::encode(code_point, std::span{made.units}.template first<codec_of<T>::max_units>());
    made.size = width_of<T> != 0 ? width_of<T> : used;
    return made;
}

// Code point order. UTF16 alone needs more than comparing units: see Utf16::rank.
template <Type T>
[[nodiscard]] constexpr std::strong_ordering compare(kept<T> a, kept<T> b) noexcept {
    if constexpr (T == Type::UTF16) {
        const auto [left, right] = std::ranges::mismatch(a, b);
        if (left == a.end()) return right == b.end() ? std::strong_ordering::equal : std::strong_ordering::less;
        if (right == b.end()) return std::strong_ordering::greater;
        return Utf16::rank(*left) <=> Utf16::rank(*right);
    } else {
        return view_of<T>(a) <=> view_of<T>(b);
    }
}

// The first place part starts, in units, from unit from on, or nothing. A match always starts where a code point
// does: in UTF8Full a match that does not start a cell is passed over (only U+0000, all zeros, can line up with a
// cell's padding); in the other Types valid text cannot start in the middle of a code point.
template <Type T>
[[nodiscard]] constexpr std::optional<std::size_t> find(kept<T> text, kept<T> part, std::size_t from = 0) noexcept {
    constexpr std::size_t step = width_of<T> != 0 ? width_of<T> : 1;
    if consteval {
        // While compiling, one place at a time: string_view's find tests a pointer for null, which g++ cannot do
        // while compiling when the pointer is into a literal's units.
        for (std::size_t at = from; at + part.size() <= text.size(); at += step) {
            if (std::ranges::equal(text.subspan(at, part.size()), part)) return at;
        }
        return std::nullopt;
    } else {
        const auto all = view_of<T>(text);
        const auto wanted = view_of<T>(part);
        std::size_t at = from;
        while (at <= all.size()) {
            const std::size_t found = all.find(wanted, at);
            if (found == units_view<T>::npos) return std::nullopt;
            if (found % step == 0) return found;
            at = found + 1;
        }
        return std::nullopt;
    }
}

// The index of the first code point above U+007F, and the code point, or nothing: whether text can be Ascii. A
// code point above U+007F always has a unit above 7F, and a unit above 7F is never part of one below U+0080.
struct NotAscii {
    std::size_t index;
    char32_t code_point;
};
template <Type T>
[[nodiscard]] constexpr std::optional<NotAscii> first_not_ascii(kept<T> text) noexcept {
    std::size_t index = 0;
    for (std::size_t at = 0; at < text.size(); at += length_at<T>(text, at)) {
        if (value_of(text[at]) >= 0x80U) return NotAscii{index, decode_at<T>(text, at)};
        ++index;
    }
    return std::nullopt;
}

// --- Converting valid kept text from one Type to another ---

// --- UTF-8 out of UTF16 and UTF32, written directly: what files, terminals and C libraries take ---

// Whether text kept as From goes to To by writing UTF-8 straight from its units.
template <Type From, Type To>
inline constexpr bool wide_to_utf8 = (From == Type::UTF16 || From == Type::UTF32) && To == Type::UTF8Packed;

// Whether a block of units are all below U+0080: their values ORed together, one test, which compilers make wide.
template <Type T>
[[nodiscard]] constexpr bool block_is_ascii(std::span<const unit_of<T>, block_units> block) noexcept {
    std::uint32_t any = 0;
    for (const auto unit : block) any |= value_of(unit);
    return any < 0x80U;
}

// How many UTF-8 bytes a unit of UTF16 or UTF32 takes: 1, and one more from each of U+0080, U+0800 and U+10000. A
// UTF-16 surrogate is half of a code point of 4 bytes: 2. Each "from" is (limit - value) >> 31, which is 1 just when
// the value is past the limit (no unit's value is near 2^31): arithmetic, not a comparison, because x86 compares
// unsigned numbers wide only by a detour, and g++ at -O2 then leaves the sum of them narrow.
template <Type From>
[[nodiscard]] constexpr std::uint32_t utf8_bytes_of(unit_of<From> unit) noexcept {
    const std::uint32_t value = value_of(unit);
    std::uint32_t bytes = 1U + ((0x7FU - value) >> 31U) + ((0x7FFU - value) >> 31U);
    if constexpr (From == Type::UTF32) {
        bytes += (0xFFFFU - value) >> 31U;
    } else {
        bytes -= (((value & 0xF800U) ^ 0xD800U) - 1U) >> 31U;
    }
    return bytes;
}

// How many UTF-8 bytes text kept as UTF16 or UTF32 takes: summed a block at a time in 32 bits, over a fixed size,
// which g++ makes wide at -O2 (one loop over the whole text it left narrow, at half the speed of walking it).
template <Type From>
[[nodiscard]] constexpr std::size_t utf8_size_from_wide(kept<From> text) noexcept {
    std::size_t size = 0;
    std::size_t at = 0;
    for (; text.size() - at >= block_units; at += block_units) {
        const auto block = text.subspan(at).template first<block_units>();
        std::uint32_t bytes = 0;
        for (const auto unit : block) bytes += utf8_bytes_of<From>(unit);
        size += bytes;
    }
    for (; at < text.size(); ++at) size += utf8_bytes_of<From>(text[at]);
    return size;
}

// Valid text kept as UTF16 or UTF32, written as UTF-8 into out, which holds exactly its size. A block of ASCII is
// written in one step; anything else a code point at a time to the end of its block, each straight into out by its
// size, with nothing decoded into or encoded out of a code point's array first.
template <Type From>
constexpr void write_utf8_from_wide(kept<From> text, std::span<char> out) noexcept {
    std::size_t to = 0;
    std::size_t at = 0;
    while (at < text.size()) {
        if (text.size() - at >= block_units && block_is_ascii<From>(text.subspan(at).template first<block_units>())) {
            const auto from = text.subspan(at).template first<block_units>();
            const auto into = out.subspan(to).template first<block_units>();
            for (std::size_t i = 0; i < block_units; ++i) into[i] = static_cast<char>(value_of(from[i]));
            at += block_units;
            to += block_units;
            continue;
        }
        const std::size_t stop = std::min(text.size(), at + block_units);
        while (at < stop) {
            std::uint32_t value = value_of(text[at]);
            ++at;
            if (value < 0x80U) {
                out[to] = static_cast<char>(value);
                to += 1;
                continue;
            }
            if (value < 0x800U) {
                out[to] = static_cast<char>(0xC0U | (value >> 6U));
                out[to + 1] = static_cast<char>(0x80U | (value & 0x3FU));
                to += 2;
                continue;
            }
            if constexpr (From == Type::UTF16) {
                if ((value & 0xFC00U) == 0xD800U) {
                    value = 0x10000U + ((value - 0xD800U) << 10U) + (value_of(text[at]) - 0xDC00U);
                    ++at;
                }
            }
            if (value < 0x10000U) {
                out[to] = static_cast<char>(0xE0U | (value >> 12U));
                out[to + 1] = static_cast<char>(0x80U | ((value >> 6U) & 0x3FU));
                out[to + 2] = static_cast<char>(0x80U | (value & 0x3FU));
                to += 3;
                continue;
            }
            out[to] = static_cast<char>(0xF0U | (value >> 18U));
            out[to + 1] = static_cast<char>(0x80U | ((value >> 12U) & 0x3FU));
            out[to + 2] = static_cast<char>(0x80U | ((value >> 6U) & 0x3FU));
            out[to + 3] = static_cast<char>(0x80U | (value & 0x3FU));
            to += 4;
        }
    }
}

// How many units text kept as From takes kept as To.
template <Type From, Type To>
[[nodiscard]] constexpr std::size_t converted_size(kept<From> text) noexcept {
    if constexpr (copies_to<From, To> || (From == Type::UTF8Packed && To == Type::Ascii)) {
        return text.size();
    } else if constexpr (To == Type::UTF8Full || width_of<To> != 0) {
        return count_of<From>(text) * max_kept<To>;
    } else if constexpr (From == Type::UTF8Full && To == Type::UTF8Packed) {
        return packed_size<Utf8>(text);
    } else if constexpr (wide_to_utf8<From, To>) {
        return utf8_size_from_wide<From>(text);
    } else {
        // A code point below U+0080 is one unit out, as in convert() below.
        std::size_t size = 0;
        for (std::size_t at = 0; at < text.size(); at += length_at<From>(text, at)) {
            size += value_of(text[at]) < 0x80U ? 1 : encode_kept<To>(decode_at<From>(text, at)).size;
        }
        return size;
    }
}

// text kept as From, written kept as To into out, which holds exactly converted_size<From, To>(text) units. To Ascii,
// text must hold nothing above U+007F.
template <Type From, Type To>
constexpr void convert(kept<From> text, std::span<unit_of<To>> out) noexcept {
    if constexpr (copies_to<From, To> || (From == Type::UTF8Packed && To == Type::Ascii)) {
        std::ranges::copy(text, out.begin());
    } else if constexpr (To == Type::UTF8Full && (From == Type::Ascii || From == Type::UTF8Packed)) {
        spread<Utf8>(view_of<From>(text), out);
    } else if constexpr (From == Type::UTF8Full && (To == Type::Ascii || To == Type::UTF8Packed)) {
        pack<Utf8>(text, out);
    } else if constexpr (wide_to_utf8<From, To>) {
        write_utf8_from_wide<From>(text, out);
    } else {
        // A code point below U+0080 is one unit with that value in every Type but UTF8Full (whose cell's first byte
        // is that value): written as one unit of To, with nothing decoded or encoded. Most text is mostly these.
        constexpr bool one_unit_out = To != Type::UTF8Full;
        std::size_t to = 0;
        std::size_t at = 0;
        while (at < text.size()) {
            if (one_unit_out && value_of(text[at]) < 0x80U) {
                out[to] = static_cast<unit_of<To>>(value_of(text[at]));
                ++to;
                at += length_at<From>(text, at);
                continue;
            }
            const Encoded<To> made = encode_kept<To>(decode_at<From>(text, at));
            std::ranges::copy(made.view(), out.subspan(to).begin());
            to += made.size;
            at += length_at<From>(text, at);
        }
    }
}

// --- Text coming in: units from outside, of T's codec (Ascii takes bytes that must all be ASCII) ---

// How many units text from outside could take kept: the most, before it is checked.
template <Type T>
[[nodiscard]] constexpr std::size_t room_for(std::size_t units) noexcept {
    return T == Type::UTF8Full ? units * Utf8::cell_units : units;
}

// Units checked and kept into out, which has room_for<T>(units.size()): how many units were kept, or where the units
// first go wrong.
template <Type T>
[[nodiscard]] constexpr std::expected<std::size_t, EncodingError> keep_checked(units_view<T> units, std::span<unit_of<T>> out) noexcept {
    if constexpr (T == Type::UTF8Full) {
        const auto cells = spread_checked<Utf8>(units, out);
        if (!cells) return std::unexpected(cells.error());
        return *cells * Utf8::cell_units;
    } else {
        const auto checked = validate<codec_of<T>>(units);
        if (!checked) return std::unexpected(checked.error());
        std::ranges::copy(units, out.begin());
        return units.size();
    }
}

// What lossy text coming in is read as: its own codec, but for Ascii, which reads UTF-8, so that é is one '?'.
template <Type T>
using lossy_source = std::conditional_t<T == Type::Ascii, Utf8, codec_of<T>>;

struct Kept {
    std::size_t units;
    std::size_t code_points;
};

// Units from outside kept into out, each bad sequence made U+FFFD (in Ascii, every code point above U+007F made '?'),
// stopping at the end of the units, when the next code point would not fit in out, or after most code points.
template <Type T>
constexpr Kept keep_lossy(std::basic_string_view<typename lossy_source<T>::unit> units, std::span<unit_of<T>> out,
                          std::size_t most = std::numeric_limits<std::size_t>::max()) noexcept {
    using source = lossy_source<T>;
    std::size_t at = 0;
    std::size_t to = 0;
    std::size_t count = 0;
    while (at < units.size() && count < most) {
        std::size_t length = 1;
        bool valid = true;
        char32_t code_point = U'\0';
        if (source::whole_alone(units[at])) {
            code_point = static_cast<char32_t>(value_of(units[at]));
        } else {
            const Scan scan = source::scan(units.substr(at));
            length = scan.length;
            valid = scan.valid;
            code_point = scan.code_point;
        }
        if constexpr (T == Type::UTF8Full) {
            if (out.size() - to < Utf8::cell_units) break;
            const auto cell = out.subspan(to).template first<Utf8::cell_units>();
            if (valid) {
                fill_cell<Utf8>(units.substr(at), length, cell);
            } else {
                fill_cell<Utf8>(Utf8::replacement(), Utf8::replacement().size(), cell);
            }
            to += Utf8::cell_units;
        } else {
            const Encoded<T> made = encode_kept<T>(valid ? code_point : replacement_character);
            if (out.size() - to < made.size) break;
            std::ranges::copy(made.view(), out.subspan(to).begin());
            to += made.size;
        }
        at += length;
        ++count;
    }
    return Kept{to, count};
}

// How many units keep_lossy keeps of all of units.
template <Type T>
[[nodiscard]] constexpr std::size_t lossy_size(std::basic_string_view<typename lossy_source<T>::unit> units) noexcept {
    using source = lossy_source<T>;
    std::size_t at = 0;
    std::size_t size = 0;
    while (at < units.size()) {
        if (source::whole_alone(units[at])) {
            size += width_of<T> != 0 ? width_of<T> : 1;
            ++at;
            continue;
        }
        const Scan scan = source::scan(units.substr(at));
        size += encode_kept<T>(scan.valid ? scan.code_point : replacement_character).size;
        at += scan.length;
    }
    return size;
}

}  // namespace detail

}  // namespace jString::inline v001
