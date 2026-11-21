// File: include/jString/v001/encoding.hpp
// Purpose: What an encoding is to jString (the Encoding concept), and the one encoding built so far: UTF-8.
//          An encoding answers one question, scan(): what is the first code point of these units, and how many
//          units does it take? Everything else (checking, counting, walking, repairing) is built on that answer.
#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>

namespace jstr::inline v001 {

// The first code point of some units. When valid is false, length is how many units the bad sequence takes (its
// maximal subpart, as the Unicode Standard counts it: chapter 3, "U+FFFD Substitution of Maximal Subparts"), and
// code_point is U+FFFD.
struct Scan {
    char32_t code_point;
    std::size_t length;
    bool valid;

    friend constexpr bool operator==(const Scan&, const Scan&) noexcept = default;
};

// Where text first stops being valid: the offset, in units, of the first unit of the first bad sequence.
struct EncodingError {
    std::size_t offset;

    friend constexpr bool operator==(const EncodingError&, const EncodingError&) noexcept = default;
};

inline constexpr char32_t replacement_character = U'�';

// An encoding: its code unit, scan() over a non-empty run of units, and the units of U+FFFD.
template <class E>
concept Encoding = std::is_trivially_copyable_v<typename E::unit> && requires(std::basic_string_view<typename E::unit> units) {
    { E::scan(units) } noexcept -> std::same_as<Scan>;
    { E::replacement() } noexcept -> std::same_as<std::basic_string_view<typename E::unit>>;
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
            if (at >= units.size()) return Scan{replacement_character, at, false};
            const std::uint32_t next = byte(units[at]);
            if (next < low || next > high) return Scan{replacement_character, at, false};
            value = (value << 6U) | (next & 0x3FU);
            low = 0x80U;
            high = 0xBFU;
        }
        return Scan{static_cast<char32_t>(value), follow + 1, true};
    }

    [[nodiscard]] static constexpr std::string_view replacement() noexcept { return "\xEF\xBF\xBD"; }

private:
    [[nodiscard]] static constexpr std::uint32_t byte(char unit) noexcept { return static_cast<unsigned char>(unit); }
};

static_assert(Encoding<Utf8>);

// The offset of the first bad sequence in units, or nothing when all of them are valid.
template <Encoding E>
[[nodiscard]] constexpr std::optional<EncodingError> first_invalid(std::basic_string_view<typename E::unit> units) noexcept {
    std::size_t at = 0;
    while (at < units.size()) {
        const Scan scan = E::scan(units.substr(at));
        if (!scan.valid) return EncodingError{at};
        at += scan.length;
    }
    return std::nullopt;
}

}  // namespace jstr::inline v001
