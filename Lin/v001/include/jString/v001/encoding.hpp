// File: include/jString/v001/encoding.hpp
// Purpose: What an encoding is to jString (the Encoding concept), and the four it knows: ASCII, UTF-8, UTF-16 and
//          UTF-32. An encoding answers two questions. scan(): what is the first code point of these units, and how
//          many units does it take? encode(): what are a code point's units? Checking, repairing and converting text
//          are built on those answers. How a String lays the units out (packed, or a code point to a 4-byte cell) is
//          type.hpp's, not the encoding's.
#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>

namespace jString::inline v001 {

// The first code point of some units. When valid is false, length is how many units the bad sequence takes (its
// maximal subpart, as the Unicode Standard counts it: chapter 3, "U+FFFD Substitution of Maximal Subparts"), and
// code_point is U+FFFD.
// Eight bytes, so that it comes back in a register: a code point or a bad sequence is never more than 4 units long.
struct Scan {
    char32_t code_point;
    std::uint8_t length;
    bool valid;

    friend constexpr bool operator==(const Scan&, const Scan&) noexcept = default;
};

// Where text first stops being valid: the offset, in units, of the first unit of the first bad sequence.
struct EncodingError {
    std::size_t offset;

    friend constexpr bool operator==(const EncodingError&, const EncodingError&) noexcept = default;
};

inline constexpr char32_t replacement_character = U'�';

// An encoding: its code unit, the most units one code point takes (max_units), scan() over a non-empty run of units,
// the units of its replacement for a bad sequence, and these about one unit, one code point, or valid units:
//   whole_alone(u):       u by itself is a whole, valid code point (in UTF-8: ASCII), so 16 in a row are checked at once
//   starts_code_point(u): in valid text, a code point starts at u (in UTF-8: anything but a continuation byte)
//   length_of(u):         in valid text, how many units the code point that starts at u takes
//   decode(valid):        the code point valid starts with, with no checking: scan() checked it on the way in
//   encode(c, out):       code point c's units, written into out: how many
template <class E>
concept Encoding = std::is_trivially_copyable_v<typename E::unit> && E::max_units >= 1 &&
                   requires(std::basic_string_view<typename E::unit> units, std::span<const typename E::unit> valid, typename E::unit unit,
                            std::span<typename E::unit, E::max_units> out) {
                       { E::scan(units) } noexcept -> std::same_as<Scan>;
                       { E::replacement() } noexcept -> std::same_as<std::basic_string_view<typename E::unit>>;
                       { E::whole_alone(unit) } noexcept -> std::same_as<bool>;
                       { E::starts_code_point(unit) } noexcept -> std::same_as<bool>;
                       { E::length_of(unit) } noexcept -> std::same_as<std::size_t>;
                       { E::decode(valid) } noexcept -> std::same_as<char32_t>;
                       { E::encode(char32_t{}, out) } noexcept -> std::same_as<std::size_t>;
                   };

// Whether a code point is one any Unicode encoding may hold: at most U+10FFFF, and not a surrogate (U+D800 to U+DFFF,
// which exist only to make UTF-16's pairs).
[[nodiscard]] constexpr bool is_scalar_value(char32_t code_point) noexcept {
    return code_point <= U'\U0010FFFF' && (code_point < U'\xD800' || code_point > U'\xDFFF');
}

// ASCII: the code points below U+0080, a byte each. Text from outside is ASCII only when every byte is below 80; it
// is never guessed at. A code point it cannot hold is written as '?', which is ASCII's own sign for a character lost.
struct Ascii {
    using unit = char;
    static constexpr std::size_t max_units = 1;

    // units must not be empty.
    [[nodiscard]] static constexpr Scan scan(std::string_view units) noexcept {
        const auto lead = static_cast<unsigned char>(units.front());
        if (lead < 0x80U) return Scan{static_cast<char32_t>(lead), 1, true};
        return Scan{replacement_character, 1, false};
    }
    [[nodiscard]] static constexpr std::string_view replacement() noexcept { return "?"; }
    [[nodiscard]] static constexpr bool whole_alone(char unit) noexcept { return static_cast<unsigned char>(unit) < 0x80U; }
    [[nodiscard]] static constexpr bool starts_code_point(char) noexcept { return true; }
    [[nodiscard]] static constexpr std::size_t length_of(char) noexcept { return 1; }
    [[nodiscard]] static constexpr char32_t decode(std::span<const char> valid) noexcept {
        return static_cast<char32_t>(static_cast<unsigned char>(valid.front()));
    }
    // Below U+0080 the code point; anything else '?'.
    [[nodiscard]] static constexpr std::size_t encode(char32_t code_point, std::span<char, 1> out) noexcept {
        out[0] = code_point < U'\x80' ? static_cast<char>(code_point) : '?';
        return 1;
    }
};

// UTF-8, held in char so that it goes to C libraries, files and std::cout unchanged. Valid means well-formed as the
// Unicode Standard's table 3-7 says: no overlong forms, no surrogates (U+D800 to U+DFFF), nothing past U+10FFFF.
struct Utf8 {
    using unit = char;
    static constexpr std::size_t max_units = 4;
    // Type::UTF8Full keeps each code point in a cell this long: the longest UTF-8 sequence, so every code point fits.
    static constexpr std::size_t cell_units = max_units;

    // units must not be empty.
    [[nodiscard]] static constexpr Scan scan(std::string_view units) noexcept {
        const std::uint32_t lead = byte(units.front());
        if (lead < 0x80U) return Scan{static_cast<char32_t>(lead), 1, true};
        // The lead byte decides how many continuation bytes follow, and the range the first of them must be in.
        std::size_t follow = 0;
        std::uint32_t low = 0x80U;
        std::uint32_t high = 0xBFU;
        std::uint32_t value = 0;
        if (lead >= 0xC2U && lead <= 0xDFU) {
            follow = 1;
            value = lead & 0x1FU;
        } else if (lead >= 0xE0U && lead <= 0xEFU) {
            follow = 2;
            value = lead & 0x0FU;
            if (lead == 0xE0U) low = 0xA0U;   // below that is an overlong form
            if (lead == 0xEDU) high = 0x9FU;  // above that is a surrogate
        } else if (lead >= 0xF0U && lead <= 0xF4U) {
            follow = 3;
            value = lead & 0x07U;
            if (lead == 0xF0U) low = 0x90U;   // below that is an overlong form
            if (lead == 0xF4U) high = 0x8FU;  // above that is past U+10FFFF
        } else {
            return Scan{replacement_character, 1, false};  // a continuation byte, C0, C1, or F5 to FF
        }
        for (std::size_t at = 1; at <= follow; ++at) {
            if (at >= units.size()) return Scan{replacement_character, static_cast<std::uint8_t>(at), false};
            const std::uint32_t next = byte(units[at]);
            if (next < low || next > high) return Scan{replacement_character, static_cast<std::uint8_t>(at), false};
            value = (value << 6U) | (next & 0x3FU);
            low = 0x80U;
            high = 0xBFU;
        }
        return Scan{static_cast<char32_t>(value), static_cast<std::uint8_t>(follow + 1), true};
    }

    [[nodiscard]] static constexpr std::string_view replacement() noexcept { return "\xEF\xBF\xBD"; }

    [[nodiscard]] static constexpr bool whole_alone(char unit) noexcept { return byte(unit) < 0x80U; }
    [[nodiscard]] static constexpr bool starts_code_point(char unit) noexcept { return (byte(unit) & 0xC0U) != 0x80U; }
    // In valid text the lead byte's high four bits say it: below C one byte (ASCII; 8 to B are never a lead), C and D
    // two, E three, F four. Looked up, not compared: compares become branches, which mixed text keeps mispredicting.
    [[nodiscard]] static constexpr std::size_t length_of(char lead) noexcept {
        // static: one table for the program, not one built on the stack at every call.
        static constexpr std::array<std::uint8_t, 16> by_high_bits{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 3, 4};
        return by_high_bits[byte(lead) >> 4U];
    }

    // valid must start with a whole, valid code point (a cell does): the lead byte gives the length and its own bits,
    // and each byte after it six more. Kept short, so that compilers put it inline in a loop over code points.
    [[nodiscard]] static constexpr char32_t decode(std::span<const char> valid) noexcept {
        const std::uint32_t lead = byte(valid.front());
        if (lead < 0x80U) return static_cast<char32_t>(lead);
        const std::size_t length = length_of(valid.front());
        std::uint32_t value = lead & (0xFFU >> (length + 1));
        for (const char unit : valid.subspan(1, length - 1)) value = (value << 6U) | (byte(unit) & 0x3FU);
        return static_cast<char32_t>(value);
    }

    // The rule in the Unicode Standard's table 3-6. code_point must be a scalar value.
    [[nodiscard]] static constexpr std::size_t encode(char32_t code_point, std::span<char, 4> out) noexcept {
        const std::uint32_t c = code_point;
        if (c < 0x80U) {
            out[0] = unit_of(c);
            return 1;
        }
        if (c < 0x800U) {
            out[0] = unit_of(0xC0U | (c >> 6U));
            out[1] = unit_of(0x80U | (c & 0x3FU));
            return 2;
        }
        if (c < 0x10000U) {
            out[0] = unit_of(0xE0U | (c >> 12U));
            out[1] = unit_of(0x80U | ((c >> 6U) & 0x3FU));
            out[2] = unit_of(0x80U | (c & 0x3FU));
            return 3;
        }
        out[0] = unit_of(0xF0U | (c >> 18U));
        out[1] = unit_of(0x80U | ((c >> 12U) & 0x3FU));
        out[2] = unit_of(0x80U | ((c >> 6U) & 0x3FU));
        out[3] = unit_of(0x80U | (c & 0x3FU));
        return 4;
    }

private:
    [[nodiscard]] static constexpr std::uint32_t byte(char unit) noexcept { return static_cast<unsigned char>(unit); }
    [[nodiscard]] static constexpr char unit_of(std::uint32_t bits) noexcept { return static_cast<char>(static_cast<unsigned char>(bits)); }
};

// UTF-16, held in char16_t in this machine's byte order. A code point above U+FFFF is a pair: a high surrogate
// (D800 to DBFF), then a low one (DC00 to DFFF). A surrogate anywhere else is a bad sequence one unit long.
struct Utf16 {
    using unit = char16_t;
    static constexpr std::size_t max_units = 2;

    // units must not be empty.
    [[nodiscard]] static constexpr Scan scan(std::u16string_view units) noexcept {
        const char16_t lead = units.front();
        if (!is_surrogate(lead)) return Scan{lead, 1, true};
        if (is_high(lead) && units.size() >= 2 && is_low(units[1])) return Scan{pair(lead, units[1]), 2, true};
        return Scan{replacement_character, 1, false};
    }
    [[nodiscard]] static constexpr std::u16string_view replacement() noexcept { return u"\uFFFD"; }
    [[nodiscard]] static constexpr bool whole_alone(char16_t unit) noexcept { return !is_surrogate(unit); }
    [[nodiscard]] static constexpr bool starts_code_point(char16_t unit) noexcept { return !is_low(unit); }
    [[nodiscard]] static constexpr std::size_t length_of(char16_t lead) noexcept { return is_high(lead) ? 2 : 1; }
    [[nodiscard]] static constexpr char32_t decode(std::span<const char16_t> valid) noexcept {
        const char16_t lead = valid.front();
        return is_high(lead) ? pair(lead, valid[1]) : char32_t{lead};
    }
    // code_point must be a scalar value.
    [[nodiscard]] static constexpr std::size_t encode(char32_t code_point, std::span<char16_t, 2> out) noexcept {
        const std::uint32_t c = code_point;
        if (c < 0x10000U) {
            out[0] = static_cast<char16_t>(c);
            return 1;
        }
        const std::uint32_t above = c - 0x10000U;
        out[0] = static_cast<char16_t>(0xD800U + (above >> 10U));
        out[1] = static_cast<char16_t>(0xDC00U + (above & 0x3FFU));
        return 2;
    }

    // Where a unit sorts so that units compare in code point order: a pair stands for a code point above every unit
    // from E000 to FFFF, so surrogates are moved above them, and E000 to FFFF down into the room.
    [[nodiscard]] static constexpr std::uint32_t rank(char16_t unit) noexcept {
        const std::uint32_t u = unit;
        if (u >= 0xE000U) return u - 0x800U;
        if (u >= 0xD800U) return u + 0x2000U;
        return u;
    }

private:
    [[nodiscard]] static constexpr bool is_surrogate(char16_t unit) noexcept { return unit >= u'\xD800' && unit <= u'\xDFFF'; }
    [[nodiscard]] static constexpr bool is_high(char16_t unit) noexcept { return unit >= u'\xD800' && unit <= u'\xDBFF'; }
    [[nodiscard]] static constexpr bool is_low(char16_t unit) noexcept { return unit >= u'\xDC00' && unit <= u'\xDFFF'; }
    [[nodiscard]] static constexpr char32_t pair(char16_t high, char16_t low) noexcept {
        const std::uint32_t h = high;
        const std::uint32_t l = low;
        return static_cast<char32_t>(0x10000U + ((h - 0xD800U) << 10U) + (l - 0xDC00U));
    }
};

// UTF-32, held in char32_t in this machine's byte order: one unit, the code point itself.
struct Utf32 {
    using unit = char32_t;
    static constexpr std::size_t max_units = 1;

    // units must not be empty.
    [[nodiscard]] static constexpr Scan scan(std::u32string_view units) noexcept {
        const char32_t unit = units.front();
        if (is_scalar_value(unit)) return Scan{unit, 1, true};
        return Scan{replacement_character, 1, false};
    }
    [[nodiscard]] static constexpr std::u32string_view replacement() noexcept { return U"\uFFFD"; }
    [[nodiscard]] static constexpr bool whole_alone(char32_t unit) noexcept { return is_scalar_value(unit); }
    [[nodiscard]] static constexpr bool starts_code_point(char32_t) noexcept { return true; }
    [[nodiscard]] static constexpr std::size_t length_of(char32_t) noexcept { return 1; }
    [[nodiscard]] static constexpr char32_t decode(std::span<const char32_t> valid) noexcept { return valid.front(); }
    // code_point must be a scalar value.
    [[nodiscard]] static constexpr std::size_t encode(char32_t code_point, std::span<char32_t, 1> out) noexcept {
        out[0] = code_point;
        return 1;
    }
};

static_assert(Encoding<Ascii>);
static_assert(Encoding<Utf8>);
static_assert(Encoding<Utf16>);
static_assert(Encoding<Utf32>);
static_assert(sizeof(Scan) == 8);

namespace detail {

// Text is checked and counted this many units at a time where it can be: a loop of a fixed length with no early exit,
// which compilers make into a few wide instructions.
inline constexpr std::size_t block_units = 16;

// Whether every unit of a block is a whole code point by itself. Written with &, not &&: g++ and clang++ both make it
// wide instructions (each was timed in four forms; && was slow on clang++, a count slow on g++).
template <Encoding E>
[[nodiscard]] constexpr bool all_whole_alone(std::basic_string_view<typename E::unit> block) noexcept {
    unsigned all = 1;
    for (const auto unit : block) all &= E::whole_alone(unit) ? 1U : 0U;
    return all != 0;
}

}  // namespace detail

// Checks units: how many code points they hold, or where they first go wrong. A block of units that are each a whole
// code point is passed in one step; anything else is scanned a code point at a time to the end of its block.
template <Encoding E>
[[nodiscard]] constexpr std::expected<std::size_t, EncodingError> validate(std::basic_string_view<typename E::unit> units) noexcept {
    std::size_t at = 0;
    std::size_t count = 0;
    while (at < units.size()) {
        if (units.size() - at >= detail::block_units && detail::all_whole_alone<E>(units.substr(at, detail::block_units))) {
            at += detail::block_units;
            count += detail::block_units;
            continue;
        }
        const std::size_t stop = std::min(units.size(), at + detail::block_units);
        while (at < stop) {
            if (E::whole_alone(units[at])) {
                ++at;
                ++count;
                continue;
            }
            const Scan scan = E::scan(units.substr(at));
            if (!scan.valid) return std::unexpected(EncodingError{at});
            at += scan.length;
            ++count;
        }
    }
    return count;
}

// The offset of the first bad sequence in units, or nothing when all of them are valid.
template <Encoding E>
[[nodiscard]] constexpr std::optional<EncodingError> first_invalid(std::basic_string_view<typename E::unit> units) noexcept {
    const auto checked = validate<E>(units);
    if (checked) return std::nullopt;
    return checked.error();
}

}  // namespace jString::inline v001

// The short name: jstr::String<jstr::Type::UTF8Packed> is jString::String<jString::Type::UTF8Packed>.
namespace jstr = jString;
