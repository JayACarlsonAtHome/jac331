// File: include/jString/v001/encoding.hpp
// Purpose: What an encoding is to jString (the Encoding concept), and the one encoding built so far: UTF-8.
//          An encoding answers one question, scan(): what is the first code point of these units, and how many
//          units does it take? Checking and repairing text that comes in are built on that answer.
//
//          jString keeps every code point in a cell of the same size, cell_units units (4 for UTF-8): the code point's
//          own units first, then zeros. Code point n is cell n, found by multiplying, never by walking the text. A cell
//          is bytes in order, not a number, so it reads the same on every machine, whatever its byte order.
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

namespace jstr::inline v001 {

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

// An encoding: its code unit, how many units a cell holds, scan() over a non-empty run of units, the units of U+FFFD,
// and three questions about one unit and one about a cell:
//   whole_alone(u):       u by itself is a whole, valid code point (in UTF-8: ASCII), so 16 in a row are checked at once
//   starts_code_point(u): in valid text, a code point starts at u (in UTF-8: anything but a continuation byte)
//   length_of(u):         in valid text, how many units the code point that starts at u takes
//   decode(cell):         the code point in a cell of valid text, with no checking: scan() checked it on the way in
template <class E>
concept Encoding = std::is_trivially_copyable_v<typename E::unit> && E::cell_units >= 1 &&
                   requires(std::basic_string_view<typename E::unit> units, std::span<const typename E::unit> cell, typename E::unit unit) {
                       { E::cell_units } -> std::convertible_to<std::size_t>;
                       { E::scan(units) } noexcept -> std::same_as<Scan>;
                       { E::replacement() } noexcept -> std::same_as<std::basic_string_view<typename E::unit>>;
                       { E::whole_alone(unit) } noexcept -> std::same_as<bool>;
                       { E::starts_code_point(unit) } noexcept -> std::same_as<bool>;
                       { E::length_of(unit) } noexcept -> std::same_as<std::size_t>;
                       { E::decode(cell) } noexcept -> std::same_as<char32_t>;
                   };

// UTF-8, held in char so that it goes to C libraries, files and std::cout unchanged. Valid means well-formed as the
// Unicode Standard's table 3-7 says: no overlong forms, no surrogates (U+D800 to U+DFFF), nothing past U+10FFFF.
struct Utf8 {
    using unit = char;
    // The longest UTF-8 sequence: every code point fits a cell of 4 bytes.
    static constexpr std::size_t cell_units = 4;

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

private:
    [[nodiscard]] static constexpr std::uint32_t byte(char unit) noexcept { return static_cast<unsigned char>(unit); }
};

static_assert(Encoding<Utf8>);
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

}  // namespace jstr::inline v001
