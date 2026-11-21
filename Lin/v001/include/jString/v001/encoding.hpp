// File: include/jString/v001/encoding.hpp
// Purpose: What an encoding is to jString (the Encoding concept), and the one encoding built so far: UTF-8.
//          An encoding answers one question, scan(): what is the first code point of these units, and how many
//          units does it take? Everything else (checking, counting, walking, repairing) is built on that answer.
#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
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

// An encoding: its code unit, scan() over a non-empty run of units, the units of U+FFFD, and two questions about one
// unit that let text be checked and counted many units at a time:
//   whole_alone(u):       u by itself is a whole, valid code point (in UTF-8: ASCII)
//   starts_code_point(u): in valid text, a code point starts at u (in UTF-8: anything but a continuation byte)
//   length_of(u):         in valid text, how many units the code point that starts at u takes
//   decode(valid):        the code point at the start of valid units, with no checking: scan() checks
template <class E>
concept Encoding = std::is_trivially_copyable_v<typename E::unit> &&
                   requires(std::basic_string_view<typename E::unit> units, typename E::unit unit) {
                       { E::scan(units) } noexcept -> std::same_as<Scan>;
                       { E::replacement() } noexcept -> std::same_as<std::basic_string_view<typename E::unit>>;
                       { E::whole_alone(unit) } noexcept -> std::same_as<bool>;
                       { E::starts_code_point(unit) } noexcept -> std::same_as<bool>;
                       { E::length_of(unit) } noexcept -> std::same_as<std::size_t>;
                       { E::decode(units) } noexcept -> std::same_as<char32_t>;
                   };

// UTF-8, held in char so that it goes to C libraries, files and std::cout unchanged. Valid means well-formed as the
// Unicode Standard's table 3-7 says: no overlong forms, no surrogates (U+D800 to U+DFFF), nothing past U+10FFFF.
struct Utf8 {
    using unit = char;

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
    // In valid text the lead byte says it: below C0 one byte (ASCII), from C0 two, from E0 three, from F0 four.
    [[nodiscard]] static constexpr std::size_t length_of(char lead) noexcept {
        const std::uint32_t b = byte(lead);
        return 1U + (b >= 0xC0U ? 1U : 0U) + (b >= 0xE0U ? 1U : 0U) + (b >= 0xF0U ? 1U : 0U);
    }

    // valid must start with a whole, valid code point: the lead byte gives the length and its own bits, and each byte
    // after it six more. Kept short, so that compilers put it inline in a loop over code points.
    [[nodiscard]] static constexpr char32_t decode(std::string_view valid) noexcept {
        const std::uint32_t lead = byte(valid.front());
        if (lead < 0x80U) return static_cast<char32_t>(lead);
        const std::size_t length = length_of(valid.front());
        std::uint32_t value = lead & (0xFFU >> (length + 1));
        for (const char unit : valid.substr(1, length - 1)) value = (value << 6U) | (byte(unit) & 0x3FU);
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

// How many code points valid units hold.
template <Encoding E>
[[nodiscard]] constexpr std::size_t code_point_count(std::basic_string_view<typename E::unit> valid) noexcept {
    std::size_t count = 0;
    for (const auto unit : valid) count += E::starts_code_point(unit) ? 1U : 0U;
    return count;
}

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

// In valid units: the offset of the code point that has skip code points before it, or the size when there are exactly
// skip of them, or nothing when there are fewer. Whole blocks are counted, not walked, while the code point is past them.
template <Encoding E>
[[nodiscard]] constexpr std::optional<std::size_t> offset_of(std::basic_string_view<typename E::unit> valid, std::size_t skip) noexcept {
    std::size_t at = 0;
    std::size_t left = skip;
    while (valid.size() - at >= detail::block_units) {
        const std::size_t here = code_point_count<E>(valid.substr(at, detail::block_units));
        if (here > left) break;
        left -= here;
        at += detail::block_units;
    }
    for (const auto unit : valid.substr(at)) {
        if (E::starts_code_point(unit)) {
            if (left == 0) return at;
            --left;
        }
        ++at;
    }
    if (left == 0) return valid.size();
    return std::nullopt;
}

}  // namespace jstr::inline v001
