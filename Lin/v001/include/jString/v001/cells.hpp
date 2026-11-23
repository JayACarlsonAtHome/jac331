// File: include/jString/v001/cells.hpp
// Purpose: The work on cells that String, FixedString and StringView share, none of it for callers to use: spreading
//          text that comes in (packed, as files and C libraries hold it) into cells, packing cells back for text that
//          goes out, and searching cells. A cell is E::cell_units units: one code point's units, then zeros.
//
//          Cells are compared and searched as bytes, never loaded as numbers: a number's order depends on the machine's
//          byte order, and the bytes' order is the order of the code points on every machine.
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>

#include "encoding.hpp"

namespace jString::inline v001::detail {

// Whether cells are 4 bytes on a machine with a known byte order: then 16 one-unit code points are spread and packed as
// 16 4-byte values, which compilers make into a few vector instructions, not 64 single bytes.
template <Encoding E>
inline constexpr bool cells_as_words = sizeof(typename E::unit) == 1 && E::cell_units == sizeof(std::uint32_t) &&
                                       (std::endian::native == std::endian::little || std::endian::native == std::endian::big);

// The 4-byte value whose bytes, in memory order, are lead then three zeros: on any machine, the cell of lead.
[[nodiscard]] constexpr std::uint32_t cell_word(unsigned char lead) noexcept {
    return std::endian::native == std::endian::little ? std::uint32_t{lead} : std::uint32_t{lead} << 24U;
}

// The lead byte of such a cell, from its value.
[[nodiscard]] constexpr unsigned char cell_lead(std::uint32_t word) noexcept {
    return static_cast<unsigned char>(std::endian::native == std::endian::little ? word : word >> 24U);
}

// Whether 16 cells each hold a code point that is whole in one unit. In UTF-8 with 4-byte cells: all 16 values ORed
// together have no bit set outside the lead byte's low 7 (ASCII leads, zero padding), one test for the block.
template <Encoding E>
[[nodiscard]] constexpr bool block_all_alone(std::span<const typename E::unit, block_units * E::cell_units> cells) noexcept {
    if constexpr (cells_as_words<E> && std::same_as<E, Utf8>) {
        std::array<typename E::unit, block_units * E::cell_units> bytes{};
        for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = cells[i];
        const auto words = std::bit_cast<std::array<std::uint32_t, block_units>>(bytes);
        std::uint32_t any = 0;
        for (const std::uint32_t word : words) any |= word;
        return (any & ~cell_word(0x7FU)) == 0;
    } else {
        unsigned all = 1;
        for (std::size_t i = 0; i < block_units; ++i) all &= E::whole_alone(cells[i * E::cell_units]) ? 1U : 0U;
        return all != 0;
    }
}

// 16 one-unit code points into their 16 cells.
template <Encoding E>
constexpr void spread_block(std::span<const typename E::unit, block_units> from, std::span<typename E::unit, block_units * E::cell_units> to) noexcept {
    if constexpr (cells_as_words<E>) {
        std::array<std::uint32_t, block_units> words{};
        for (std::size_t i = 0; i < block_units; ++i) words[i] = cell_word(static_cast<unsigned char>(from[i]));
        const auto bytes = std::bit_cast<std::array<typename E::unit, block_units * E::cell_units>>(words);
        for (std::size_t i = 0; i < bytes.size(); ++i) to[i] = bytes[i];
    } else {
        for (std::size_t i = 0; i < block_units; ++i) {
            to[i * E::cell_units] = from[i];
            for (std::size_t rest = 1; rest < E::cell_units; ++rest) to[i * E::cell_units + rest] = typename E::unit{};
        }
    }
}

// 16 cells that each hold a one-unit code point, packed: their lead units.
template <Encoding E>
constexpr void pack_block(std::span<const typename E::unit, block_units * E::cell_units> from, std::span<typename E::unit, block_units> to) noexcept {
    if constexpr (cells_as_words<E>) {
        std::array<typename E::unit, block_units * E::cell_units> bytes{};
        for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = from[i];
        const auto words = std::bit_cast<std::array<std::uint32_t, block_units>>(bytes);
        for (std::size_t i = 0; i < block_units; ++i) to[i] = static_cast<typename E::unit>(cell_lead(words[i]));
    } else {
        for (std::size_t i = 0; i < block_units; ++i) to[i] = from[i * E::cell_units];
    }
}

// One code point's units, written into its cell: the units, then zeros to the end of the cell. Where a whole cell's
// worth of units can be read, they are read at once as one 4-byte value and the units past the code point masked to
// zero, with no branch on the length. The value is made back into bytes in their order, and the mask is chosen by the
// machine's byte order, so the cell's bytes are the same on every machine. units must be valid text from the code
// point on: only its first length units are read otherwise.
template <Encoding E>
constexpr void fill_cell(std::basic_string_view<typename E::unit> units, std::size_t length, std::span<typename E::unit, E::cell_units> cell) noexcept {
    using cell_array = std::array<typename E::unit, E::cell_units>;
    cell_array made{};
    if constexpr (sizeof(cell_array) == sizeof(std::uint32_t) && (std::endian::native == std::endian::little || std::endian::native == std::endian::big)) {
        if (units.size() >= E::cell_units) {
            for (std::size_t k = 0; k < E::cell_units; ++k) made[k] = units[k];
            const auto value = std::bit_cast<std::uint32_t>(made);
            const unsigned drop = static_cast<unsigned>(8U * (E::cell_units - length));
            const std::uint32_t keep = std::endian::native == std::endian::little ? 0xFFFFFFFFU >> drop : 0xFFFFFFFFU << drop;
            made = std::bit_cast<cell_array>(value & keep);
            for (std::size_t k = 0; k < E::cell_units; ++k) cell[k] = made[k];
            return;
        }
    }
    for (std::size_t k = 0; k < length; ++k) made[k] = units[k];
    for (std::size_t k = 0; k < E::cell_units; ++k) cell[k] = made[k];
}

// Valid units, spread into cells: out holds exactly one cell for each of their code points. 16 units that are each a
// whole code point (in UTF-8, ASCII) are spread in one step, over spans of a fixed size, which compilers make into a
// few wide instructions. A block that is not all such units is spread a code point at a time to its end, before the
// next block is tried, so text that is mostly wide code points is not checked 16 units ahead at every one.
template <Encoding E>
constexpr void spread(std::basic_string_view<typename E::unit> valid, std::span<typename E::unit> out) noexcept {
    constexpr std::size_t width = E::cell_units;
    const std::span<const typename E::unit> in{valid};
    std::size_t at = 0;
    std::size_t cell = 0;
    while (at < valid.size()) {
        if (valid.size() - at >= block_units && all_whole_alone<E>(valid.substr(at, block_units))) {
            spread_block<E>(in.subspan(at).template first<block_units>(), out.subspan(cell * width).template first<block_units * width>());
            at += block_units;
            cell += block_units;
            continue;
        }
        const std::size_t stop = std::min(valid.size(), at + block_units);
        while (at < stop) {
            const std::size_t length = E::length_of(valid[at]);
            fill_cell<E>(valid.substr(at), length, out.subspan(cell * width).template first<width>());
            at += length;
            ++cell;
        }
    }
}

// Units checked and spread into cells in one pass: how many code points they hold, or where they first go wrong. out
// must have room for a cell for every unit, the most code points units can hold. Checking and spreading walk the same
// chain (where each code point starts depends on the length of the one before), so one walk costs about half of two.
template <Encoding E>
[[nodiscard]] constexpr std::expected<std::size_t, EncodingError> spread_checked(std::basic_string_view<typename E::unit> units,
                                                                                 std::span<typename E::unit> out) noexcept {
    constexpr std::size_t width = E::cell_units;
    const std::span<const typename E::unit> in{units};
    std::size_t at = 0;
    std::size_t cell = 0;
    while (at < units.size()) {
        if (units.size() - at >= block_units && all_whole_alone<E>(units.substr(at, block_units))) {
            spread_block<E>(in.subspan(at).template first<block_units>(), out.subspan(cell * width).template first<block_units * width>());
            at += block_units;
            cell += block_units;
            continue;
        }
        const std::size_t stop = std::min(units.size(), at + block_units);
        while (at < stop) {
            std::size_t length = 1;
            if (!E::whole_alone(units[at])) {
                const Scan scan = E::scan(units.substr(at));
                if (!scan.valid) return std::unexpected(EncodingError{at});
                length = scan.length;
            }
            fill_cell<E>(units.substr(at), length, out.subspan(cell * width).template first<width>());
            at += length;
            ++cell;
        }
    }
    return cell;
}

// How many code points units hold when each bad sequence is one U+FFFD.
template <Encoding E>
[[nodiscard]] constexpr std::size_t lossy_count(std::basic_string_view<typename E::unit> units) noexcept {
    std::size_t at = 0;
    std::size_t count = 0;
    while (at < units.size()) {
        at += E::whole_alone(units[at]) ? std::size_t{1} : E::scan(units.substr(at)).length;
        ++count;
    }
    return count;
}

// Units spread into cells, each bad sequence made one U+FFFD. Stops when out is full: out may hold fewer cells than
// the units have code points, which is how a FixedString cuts its text. Gives how many cells were written.
template <Encoding E>
constexpr std::size_t spread_lossy(std::basic_string_view<typename E::unit> units, std::span<typename E::unit> out) noexcept {
    constexpr std::size_t width = E::cell_units;
    const std::size_t cells = out.size() / width;
    std::size_t at = 0;
    std::size_t cell = 0;
    while (at < units.size() && cell < cells) {
        const auto target = out.subspan(cell * width).template first<width>();
        if (E::whole_alone(units[at])) {
            fill_cell<E>(units.substr(at), 1, target);
            ++at;
        } else {
            const Scan scan = E::scan(units.substr(at));
            if (scan.valid) {
                fill_cell<E>(units.substr(at), scan.length, target);
            } else {
                fill_cell<E>(E::replacement(), E::replacement().size(), target);
            }
            at += scan.length;
        }
        ++cell;
    }
    return cell;
}

// How many units cells take once packed: each cell's lead unit says how many of its units are the code point's.
// 16 cells that each hold a one-unit code point are 16 units, found in one test.
template <Encoding E>
[[nodiscard]] constexpr std::size_t packed_size(std::span<const typename E::unit> cells) noexcept {
    constexpr std::size_t block = block_units * E::cell_units;
    std::size_t size = 0;
    std::size_t at = 0;
    for (; cells.size() - at >= block; at += block) {
        const auto from = cells.subspan(at).template first<block>();
        if (block_all_alone<E>(from)) {
            size += block_units;
            continue;
        }
        for (std::size_t i = 0; i < block; i += E::cell_units) size += E::length_of(from[i]);
    }
    for (; at < cells.size(); at += E::cell_units) size += E::length_of(cells[at]);
    return size;
}

// Cells packed into out, which must hold packed_size(cells) units. 16 cells that each hold a code point that is whole
// in one unit (in UTF-8, ASCII) are packed in one step; a block that is not all such cells is packed a cell at a time
// to its end before the next block is tried. A cell is read into a local array, which nothing else can point into, so
// compilers read and write it as one 4-byte value. While out has room for a whole cell after the place written to, the
// whole cell is written and the place moves on by the code point's length, so the next cell writes over the zeros;
// only the last few code points are written to their exact length.
template <Encoding E>
constexpr void pack(std::span<const typename E::unit> cells, std::span<typename E::unit> out) noexcept {
    constexpr std::size_t width = E::cell_units;
    std::size_t at = 0;
    std::size_t to = 0;
    while (at < cells.size()) {
        std::size_t stop = cells.size();
        if (cells.size() - at >= block_units * width) {
            const auto from = cells.subspan(at).template first<block_units * width>();
            if (block_all_alone<E>(from)) {
                pack_block<E>(from, out.subspan(to).template first<block_units>());
                at += block_units * width;
                to += block_units;
                continue;
            }
            stop = at + block_units * width;
        }
        while (at < stop) {
            std::array<typename E::unit, width> cell{};
            const auto from = cells.subspan(at).template first<width>();
            for (std::size_t k = 0; k < width; ++k) cell[k] = from[k];
            const std::size_t length = E::length_of(cell[0]);
            if (out.size() - to >= width) {
                const auto whole = out.subspan(to).template first<width>();
                for (std::size_t k = 0; k < width; ++k) whole[k] = cell[k];
            } else {
                const auto exact = out.subspan(to, length);
                for (std::size_t k = 0; k < length; ++k) exact[k] = cell[k];
            }
            at += width;
            to += length;
        }
    }
}

// The bytes of cells, for comparing, searching and hashing them inside jString. Never handed to a caller: outside,
// the units of a cell are not text.
template <Encoding E>
[[nodiscard]] constexpr std::basic_string_view<typename E::unit> bytes_of(std::span<const typename E::unit> cells) noexcept {
    return std::basic_string_view<typename E::unit>{cells.begin(), cells.end()};
}

// The first cell where part's cells begin, from cell from on, or nothing. A match of the bytes that does not start at
// a cell's first unit is not a match (only U+0000, whose lead unit is a zero, can line up with a cell's padding).
template <Encoding E>
[[nodiscard]] constexpr std::optional<std::size_t> find_cells(std::span<const typename E::unit> cells, std::span<const typename E::unit> part,
                                                              std::size_t from = 0) noexcept {
    constexpr std::size_t width = E::cell_units;
    if consteval {
        // While compiling, cell by cell: string_view's find tests a pointer for null, which g++ cannot do while
        // compiling when the pointer is into a literal's cells.
        for (std::size_t at = from * width; at + part.size() <= cells.size(); at += width) {
            if (std::ranges::equal(cells.subspan(at, part.size()), part)) return at / width;
        }
        return std::nullopt;
    } else {
        // At run time, string_view's find, which looks for the first byte with memchr.
        const auto all = bytes_of<E>(cells);
        const auto wanted = bytes_of<E>(part);
        std::size_t at = from * width;
        while (at <= all.size()) {
            const std::size_t found = all.find(wanted, at);
            if (found == std::basic_string_view<typename E::unit>::npos) return std::nullopt;
            if (found % width == 0) return found / width;
            at = found + 1;
        }
        return std::nullopt;
    }
}

}  // namespace jString::inline v001::detail
