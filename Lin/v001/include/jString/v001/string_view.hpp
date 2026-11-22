// File: include/jString/v001/string_view.hpp
// Purpose: StringView<E>: text someone else owns, always valid in encoding E (UTF-8 unless named): a window onto cells,
//          one code point to a cell (see encoding.hpp). There is no way to make one from units that were not checked:
//          it is had only from a String, a FixedString, another StringView, or a "text"_js literal, whose cells the
//          compiler makes and checks. Text from outside comes in through String::from(), which checks it.
//
//          It has no operator[] and no offsets in units, so nothing can cut a code point in two. It never hands out its
//          cells as units: text goes out only packed, by name (to_utf8(), write_utf8(), to_c_string()), the form files
//          and C libraries read. Every position is found by multiplying, never by walking the text.
#pragma once

#include <array>
#include <compare>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <optional>
#include <ostream>
#include <ranges>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "cells.hpp"
#include "encoding.hpp"

namespace jstr::inline v001 {

// How many cells a String holds inside itself before its text goes to the heap: 16 cells of 4 bytes, 64 bytes, one
// cache line.
inline constexpr std::size_t default_inline_cells = 16;

template <Encoding E = Utf8, std::size_t InlineCells = default_inline_cells>
class String;

template <std::size_t MaxCodePoints, Encoding E = Utf8>
class FixedString;

template <Encoding E>
class Literal;

namespace detail {

// Never defined, and not constexpr: a constant evaluation that reaches one stops there, and the compiler names it.
void the_literal_is_not_valid_text();

// A string literal as a template argument, so that its cells can be made while compiling and kept for the whole run.
template <std::size_t N>
struct LiteralUnits {
    consteval LiteralUnits(const char (&literal)[N]) noexcept { std::ranges::copy(literal, units.begin()); }
    std::array<char, N> units{};  // with the literal's terminating zero, which is not text

    [[nodiscard]] consteval std::string_view text() const noexcept { return std::string_view{units.begin(), units.end()}.substr(0, N - 1); }
};

// Made from a string literal, its size taken from the literal.
template <std::size_t N>
LiteralUnits(const char (&)[N]) -> LiteralUnits<N>;

struct LiteralAccess;

}  // namespace detail

// How a position counts: from 0, as C++ containers do (an offset: how many to skip), or from 1, as people do (the 7th).
enum class IndexBase : unsigned char { zero_based, one_based };

// A position counted in code points, never in units, from 0 unless the base says 1. Explicit, so a plain number, or an
// offset in units, cannot stand in for one. CodePointPosition{6} and CodePointPosition<IndexBase::one_based>{7} are the
// same code point.
template <IndexBase Base = IndexBase::zero_based>
struct CodePointPosition {
    constexpr explicit CodePointPosition(std::size_t at) noexcept : value{at} {}
    std::size_t value;

    // How many code points come before it: nothing for one_based 0, which is no position at all.
    [[nodiscard]] constexpr std::optional<std::size_t> skipped() const noexcept {
        if constexpr (Base == IndexBase::zero_based) {
            return value;
        } else {
            if (value == 0) return std::nullopt;
            return value - 1;
        }
    }

    friend constexpr bool operator==(CodePointPosition, CodePointPosition) noexcept = default;
};

// CodePointPosition{6} counts from 0: the base is the optional part, and 0 is the default.
CodePointPosition(std::size_t) -> CodePointPosition<IndexBase::zero_based>;

using CodePointIndex = CodePointPosition<IndexBase::zero_based>;
using CodePointNumber = CodePointPosition<IndexBase::one_based>;

// How many code points: a length, never a position, so the two cannot be swapped.
struct CodePointCount {
    constexpr explicit CodePointCount(std::size_t count) noexcept : value{count} {}
    std::size_t value;

    friend constexpr bool operator==(CodePointCount, CodePointCount) noexcept = default;
};

// The code points of valid text, in order: a forward range of char32_t.
template <Encoding E>
class CodePoints : public std::ranges::view_interface<CodePoints<E>> {
public:
    using cells_span = std::span<const typename E::unit>;

    class iterator {
    public:
        using value_type = char32_t;
        using difference_type = std::ptrdiff_t;
        using iterator_concept = std::forward_iterator_tag;

        constexpr iterator() noexcept = default;

        // Valid text: decoded with no checking.
        [[nodiscard]] constexpr char32_t operator*() const noexcept { return E::decode(rest_.first(E::cell_units)); }
        constexpr iterator& operator++() noexcept {
            rest_ = rest_.subspan(E::cell_units);
            return *this;
        }
        constexpr iterator operator++(int) noexcept {
            const iterator before = *this;
            ++*this;
            return before;
        }

        // Two iterators are equal at the same place in the same text: compared by where they are, not what they hold.
        [[nodiscard]] friend constexpr bool operator==(const iterator& a, const iterator& b) noexcept {
            return a.rest_.data() == b.rest_.data() && a.rest_.size() == b.rest_.size();
        }
        [[nodiscard]] friend constexpr bool operator==(const iterator& at, std::default_sentinel_t) noexcept { return at.rest_.empty(); }

    private:
        friend CodePoints;
        constexpr explicit iterator(cells_span rest) noexcept : rest_{rest} {}
        cells_span rest_{};
    };

    [[nodiscard]] constexpr iterator begin() const noexcept { return iterator{cells_}; }
    [[nodiscard]] constexpr std::default_sentinel_t end() const noexcept { return {}; }

private:
    template <Encoding>
    friend class StringView;
    constexpr explicit CodePoints(cells_span cells) noexcept : cells_{cells} {}
    cells_span cells_{};
};

template <Encoding E = Utf8>
class StringView {
public:
    using encoding = E;
    using unit = typename E::unit;
    static constexpr std::size_t cell_units = E::cell_units;

    constexpr StringView() noexcept = default;

    [[nodiscard]] constexpr bool empty() const noexcept { return cells_.empty(); }
    // A cell to each code point, so counting is dividing.
    [[nodiscard]] constexpr std::size_t count_code_points() const noexcept { return cells_.size() / cell_units; }
    [[nodiscard]] constexpr CodePoints<E> code_points() const noexcept { return CodePoints<E>{cells_}; }

    // The one code point at a position, as text, or nothing when the text is shorter.
    template <IndexBase Base>
    [[nodiscard]] constexpr std::optional<StringView> at(CodePointPosition<Base> position) const noexcept {
        return substring(position, CodePointCount{1});
    }

    // count code points from start. Nothing unless all of them are in the text: never a shorter piece. A start just
    // past the last code point, with a count of 0, is the empty text at the end.
    template <IndexBase Base>
    [[nodiscard]] constexpr std::optional<StringView> substring(CodePointPosition<Base> start, CodePointCount count) const noexcept {
        const auto first = start.skipped();
        const std::size_t have = count_code_points();
        if (!first || *first > have || count.value > have - *first) return std::nullopt;
        return StringView{cells_.subspan(*first * cell_units, count.value * cell_units)};
    }

    // From start to the end.
    template <IndexBase Base>
    [[nodiscard]] constexpr std::optional<StringView> substring(CodePointPosition<Base> start) const noexcept {
        const auto first = start.skipped();
        if (!first || *first > count_code_points()) return std::nullopt;
        return StringView{cells_.subspan(*first * cell_units)};
    }

    // A new String: this text with count code points from start replaced by with. Nothing unless all of them are in
    // the text. This text is not changed. (Needs string.hpp, which jstring.hpp includes.)
    template <IndexBase Base>
    [[nodiscard]] constexpr std::optional<String<E>> replace(CodePointPosition<Base> start, CodePointCount count, StringView with) const {
        const auto first = start.skipped();
        const std::size_t have = count_code_points();
        if (!first || *first > have || count.value > have - *first) return std::nullopt;
        String<E> made;
        made.reserve_code_points(have - count.value + with.count_code_points());
        made += StringView{cells_.first(*first * cell_units)};
        made += with;
        made += StringView{cells_.subspan((*first + count.value) * cell_units)};
        return made;
    }

    // Cells start where code points start, so comparing the bytes of whole cells answers these exactly.
    [[nodiscard]] constexpr bool starts_with(StringView prefix) const noexcept { return bytes().starts_with(prefix.bytes()); }
    [[nodiscard]] constexpr bool ends_with(StringView suffix) const noexcept { return bytes().ends_with(suffix.bytes()); }
    [[nodiscard]] constexpr bool contains(StringView part) const noexcept { return detail::find_cells<E>(cells_, part.cells_).has_value(); }

    // The text before and after the first separator. Nothing when it is not there, or when the separator is empty,
    // which is never the separator anyone meant.
    [[nodiscard]] constexpr std::optional<std::pair<StringView, StringView>> split_once(StringView separator) const noexcept {
        if (separator.empty()) return std::nullopt;
        const auto at = detail::find_cells<E>(cells_, separator.cells_);
        if (!at) return std::nullopt;
        return std::pair{StringView{cells_.first(*at * cell_units)}, StringView{cells_.subspan(*at * cell_units + separator.cells_.size())}};
    }

    // A cell is the code point's units then zeros, and UTF-8's units are in code point order, so comparing cells as
    // bytes (unsigned, as char_traits compares them) is code point order.
    [[nodiscard]] friend constexpr bool operator==(StringView a, StringView b) noexcept { return a.bytes() == b.bytes(); }
    [[nodiscard]] friend constexpr std::strong_ordering operator<=>(StringView a, StringView b) noexcept { return a.bytes() <=> b.bytes(); }

    // Whether the text is exactly this one code point. Only a char32_t: view == 7 must not compile.
    template <std::same_as<char32_t> C>
    [[nodiscard]] friend constexpr bool operator==(StringView text, C code_point) noexcept {
        return text.count_code_points() == 1 && E::decode(text.cells_) == code_point;
    }

    // --- Text going out, packed: the one way out of jString ---

    // How many units the text takes packed (in UTF-8: bytes).
    [[nodiscard]] constexpr std::size_t size_packed() const noexcept { return detail::packed_size<E>(cells_); }

    // The text packed into out, as a file or C library reads it, with no terminating zero. Gives how many units were
    // written, or nothing, with nothing written, when out is too small.
    [[nodiscard]] constexpr std::optional<std::size_t> write_packed(std::span<unit> out) const noexcept {
        const std::size_t size = size_packed();
        if (out.size() < size) return std::nullopt;
        detail::pack<E>(cells_, out.first(size));
        return size;
    }

    // The text packed into units of its own, with no terminating zero.
    [[nodiscard]] constexpr std::vector<unit> to_packed() const {
        std::vector<unit> out(size_packed());
        detail::pack<E>(cells_, out);
        return out;
    }

    // For a C library: the text packed, then a terminating zero. Nothing when the text holds a zero, because C would
    // stop there and quietly lose the rest.
    [[nodiscard]] constexpr std::optional<std::vector<unit>> to_c_string() const {
        if (detail::find_cells<E>(cells_, zero_cell()).has_value()) return std::nullopt;
        std::vector<unit> out(size_packed() + 1);
        detail::pack<E>(cells_, std::span{out}.first(out.size() - 1));
        return out;
    }

    // In UTF-8 the packed text is UTF-8: these say so by name.
    [[nodiscard]] constexpr std::size_t size_utf8() const noexcept
        requires std::same_as<E, Utf8>
    {
        return size_packed();
    }
    [[nodiscard]] constexpr std::optional<std::size_t> write_utf8(std::span<char> out) const noexcept
        requires std::same_as<E, Utf8>
    {
        return write_packed(out);
    }
    [[nodiscard]] constexpr std::vector<char> to_utf8() const
        requires std::same_as<E, Utf8>
    {
        return to_packed();
    }

    // Printed packed, a piece at a time, with nothing made on the heap.
    friend std::ostream& operator<<(std::ostream& out, StringView text)
        requires std::same_as<unit, char>
    {
        std::array<char, 256> piece{};
        std::size_t filled = 0;
        for (std::size_t at = 0; at < text.cells_.size(); at += cell_units) {
            const std::size_t length = E::length_of(text.cells_[at]);
            if (filled + length > piece.size()) {
                out.write(piece.data(), static_cast<std::streamsize>(filled));
                filled = 0;
            }
            std::ranges::copy(text.cells_.subspan(at, length), std::span{piece}.subspan(filled).begin());
            filled += length;
        }
        return out.write(piece.data(), static_cast<std::streamsize>(filled));
    }

private:
    template <Encoding, std::size_t>
    friend class String;
    template <std::size_t, Encoding>
    friend class FixedString;
    friend Literal<E>;
    friend detail::LiteralAccess;
    friend struct std::hash<StringView>;

    // Only for cells of valid text.
    constexpr explicit StringView(std::span<const unit> cells) noexcept : cells_{cells} {}

    [[nodiscard]] constexpr std::basic_string_view<unit> bytes() const noexcept { return detail::bytes_of<E>(cells_); }
    [[nodiscard]] constexpr std::span<const unit> cells() const noexcept { return cells_; }

    // U+0000's cell: all zeros.
    [[nodiscard]] static constexpr std::span<const unit> zero_cell() noexcept {
        static constexpr std::array<unit, cell_units> zero{};
        return zero;
    }

    std::span<const unit> cells_{};
};

// Text that is known at compile time, checked then, and lives as long as the program: what "text"_js makes. Its cells
// are made while compiling, once for each literal. A String is made from one without asking (String<> s = "text"_js;),
// and a function taking a StringView takes one ("text"_js) as it is. A plain "text" is not text: it has no cells.
template <Encoding E = Utf8>
class Literal {
public:
    [[nodiscard]] constexpr StringView<E> view() const noexcept { return view_; }
    constexpr operator StringView<E>() const noexcept { return view_; }

    // A literal compared with a literal, as text: "a"_js == "a"_js.
    [[nodiscard]] friend constexpr bool operator==(Literal a, Literal b) noexcept { return a.view_ == b.view_; }

private:
    friend detail::LiteralAccess;
    constexpr explicit Literal(StringView<E> view) noexcept : view_{view} {}
    StringView<E> view_{};
};

namespace detail {

// How many code points a literal holds; a literal that is not valid text stops the compile here.
template <LiteralUnits L>
[[nodiscard]] consteval std::size_t literal_count() {
    const auto checked = validate<Utf8>(L.text());
    if (!checked) the_literal_is_not_valid_text();
    return *checked;
}

// A literal's cells, made in place while compiling (a returned array would be a copy clang++ warns it cannot leave out).
template <std::size_t Count>
struct LiteralCells {
    consteval explicit LiteralCells(std::string_view text) noexcept { spread<Utf8>(text, cells); }
    std::array<char, Count * Utf8::cell_units> cells{};
};

// A literal's cells, kept for the whole run.
template <LiteralUnits L>
inline constexpr LiteralCells<literal_count<L>()> literal_cells{L.text()};

struct LiteralAccess {
    template <LiteralUnits L>
    [[nodiscard]] static consteval Literal<Utf8> make() noexcept {
        return Literal<Utf8>{StringView<Utf8>{std::span<const char>{literal_cells<L>.cells}}};
    }
};

}  // namespace detail

namespace literals {

// "text"_js: a UTF-8 literal, checked while compiling, its cells made then.
template <detail::LiteralUnits L>
[[nodiscard]] consteval Literal<Utf8> operator""_js() noexcept {
    return detail::LiteralAccess::make<L>();
}

}  // namespace literals

}  // namespace jstr::inline v001

template <jstr::Encoding E>
struct std::hash<jstr::StringView<E>> {
    [[nodiscard]] std::size_t operator()(jstr::StringView<E> text) const noexcept {
        return std::hash<std::basic_string_view<typename E::unit>>{}(text.bytes());
    }
};
