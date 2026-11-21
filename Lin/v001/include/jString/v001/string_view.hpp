// File: include/jString/v001/string_view.hpp
// Purpose: StringView<E>: text someone else owns, always valid in encoding E (UTF-8 unless named). There is no way to
//          make one from units that were not checked:
//            - a string literal is checked by the compiler: StringView<> v = "café"; an invalid one does not compile
//            - anything else goes through StringView<>::from(units), which says where the text first goes wrong
//          It has no operator[] and no byte offsets, so nothing can cut a code point in two. Its units are reached only
//          by name, units(), where a C library or a file needs them.
#pragma once

#include <compare>
#include <concepts>
#include <cstddef>
#include <expected>
#include <iterator>
#include <optional>
#include <ostream>
#include <ranges>
#include <span>
#include <string_view>
#include <utility>

#include "encoding.hpp"

namespace jstr::inline v001 {

// How many units a String holds inside itself, its terminating zero among them, before its text goes to the heap.
inline constexpr std::size_t default_inline_units = 64;

template <Encoding E = Utf8, std::size_t InlineUnits = default_inline_units>
class String;
template <Encoding E>
class Literal;

namespace detail {

// Never defined, and not constexpr: a constant evaluation that reaches one stops there, and the compiler names it.
void the_literal_is_not_valid_text();
void the_literal_has_no_terminating_zero();

// Checks a literal while it is being compiled. units are the literal's, without its terminating zero.
template <Encoding E>
consteval std::basic_string_view<typename E::unit> checked_literal(std::basic_string_view<typename E::unit> units) {
    if (first_invalid<E>(units)) the_literal_is_not_valid_text();
    return units;
}

template <Encoding E, std::size_t N>
consteval std::basic_string_view<typename E::unit> checked_literal(const typename E::unit (&literal)[N]) {
    const std::span<const typename E::unit, N> all{literal};
    if (all.back() != typename E::unit{}) the_literal_has_no_terminating_zero();
    return checked_literal<E>(std::basic_string_view<typename E::unit>{all.template first<N - 1>()});
}

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
    using units_view = std::basic_string_view<typename E::unit>;

    class iterator {
    public:
        using value_type = char32_t;
        using difference_type = std::ptrdiff_t;
        using iterator_concept = std::forward_iterator_tag;

        constexpr iterator() noexcept = default;

        // Valid text: decoded with no checking, and its first unit says how long it is.
        [[nodiscard]] constexpr char32_t operator*() const noexcept { return E::decode(rest_); }
        constexpr iterator& operator++() noexcept {
            rest_.remove_prefix(E::length_of(rest_.front()));
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
        constexpr explicit iterator(units_view rest) noexcept : rest_{rest} {}
        units_view rest_{};
    };

    [[nodiscard]] constexpr iterator begin() const noexcept { return iterator{units_}; }
    [[nodiscard]] constexpr std::default_sentinel_t end() const noexcept { return {}; }

private:
    template <Encoding>
    friend class StringView;
    constexpr explicit CodePoints(units_view units) noexcept : units_{units} {}
    units_view units_{};
};

template <Encoding E = Utf8>
class StringView {
public:
    using encoding = E;
    using unit = typename E::unit;
    using units_view = std::basic_string_view<unit>;

    constexpr StringView() noexcept = default;

    // A string literal, checked while compiling. Implicit, so that a function taking a StringView takes "text".
    template <std::size_t N>
    consteval StringView(const unit (&literal)[N]) noexcept : units_{detail::checked_literal<E>(literal)} {}

    // Units from anywhere else: valid text, or where it first goes wrong.
    [[nodiscard]] static constexpr std::expected<StringView, EncodingError> from(units_view units) noexcept {
        if (const auto bad = first_invalid<E>(units)) return std::unexpected(*bad);
        return StringView{units};
    }

    [[nodiscard]] constexpr units_view units() const noexcept { return units_; }
    [[nodiscard]] constexpr std::size_t size_units() const noexcept { return units_.size(); }
    [[nodiscard]] constexpr bool empty() const noexcept { return units_.empty(); }

    [[nodiscard]] constexpr CodePoints<E> code_points() const noexcept { return CodePoints<E>{units_}; }
    [[nodiscard]] constexpr std::size_t count_code_points() const noexcept { return code_point_count<E>(units_); }

    // The one code point at a position, as text, or nothing when the text is shorter. A view keeps no table of where
    // its code points start, so this counts from the start, 16 units at a step; a String with its text on the heap
    // keeps one, and answers at once.
    template <IndexBase Base>
    [[nodiscard]] constexpr std::optional<StringView> at(CodePointPosition<Base> position) const noexcept {
        return substring(position, CodePointCount{1});
    }

    // count code points from start. Nothing unless all of them are in the text: never a shorter piece. A start just
    // past the last code point, with a count of 0, is the empty text at the end.
    template <IndexBase Base>
    [[nodiscard]] constexpr std::optional<StringView> substring(CodePointPosition<Base> start, CodePointCount count) const noexcept {
        const auto span = unit_span(start, count.value);
        if (!span) return std::nullopt;
        return StringView{units_.substr(span->first, span->second - span->first)};
    }

    // From start to the end.
    template <IndexBase Base>
    [[nodiscard]] constexpr std::optional<StringView> substring(CodePointPosition<Base> start) const noexcept {
        const auto span = unit_span(start, 0);
        if (!span) return std::nullopt;
        return StringView{units_.substr(span->first)};
    }

    // A new String: this text with count code points from start replaced by with. Nothing unless all of them are in
    // the text. This text is not changed. (Needs string.hpp, which jstring.hpp includes.)
    template <IndexBase Base>
    [[nodiscard]] constexpr std::optional<String<E>> replace(CodePointPosition<Base> start, CodePointCount count, StringView with) const {
        const auto span = unit_span(start, count.value);
        if (!span) return std::nullopt;
        String<E> made;
        made.reserve_units(span->first + with.size_units() + (units_.size() - span->second));
        made += StringView{units_.substr(0, span->first)};
        made += with;
        made += StringView{units_.substr(span->second)};
        return made;
    }

    // Valid text cut at the end of valid text is cut between code points, so comparing units answers these exactly.
    [[nodiscard]] constexpr bool starts_with(StringView prefix) const noexcept { return units_.starts_with(prefix.units_); }
    [[nodiscard]] constexpr bool ends_with(StringView suffix) const noexcept { return units_.ends_with(suffix.units_); }
    [[nodiscard]] constexpr bool contains(StringView part) const noexcept { return units_.find(part.units_) != units_view::npos; }

    // The text before and after the first separator. Nothing when it is not there, or when the separator is empty,
    // which is never the separator anyone meant.
    [[nodiscard]] constexpr std::optional<std::pair<StringView, StringView>> split_once(StringView separator) const noexcept {
        if (separator.empty()) return std::nullopt;
        const std::size_t at = units_.find(separator.units_);
        if (at == units_view::npos) return std::nullopt;
        return std::pair{StringView{units_.substr(0, at)}, StringView{units_.substr(at + separator.units_.size())}};
    }

    // In UTF-8, the order of the units is the order of the code points.
    [[nodiscard]] friend constexpr bool operator==(StringView a, StringView b) noexcept { return a.units_ == b.units_; }
    [[nodiscard]] friend constexpr auto operator<=>(StringView a, StringView b) noexcept { return a.units_ <=> b.units_; }
    // Whether the text is exactly this one code point. Only a char32_t: view == 7 must not compile.
    template <std::same_as<char32_t> C>
    [[nodiscard]] friend constexpr bool operator==(StringView text, C code_point) noexcept {
        if (text.empty()) return false;
        const Scan first = E::scan(text.units_);
        return first.length == text.units_.size() && first.code_point == code_point;
    }

    friend std::ostream& operator<<(std::ostream& out, StringView text)
        requires std::same_as<unit, char>
    {
        return out << text.units_;
    }

private:
    template <Encoding, std::size_t>
    friend class String;
    friend Literal<E>;
    // Only for units already known to be valid.
    constexpr explicit StringView(units_view units) noexcept : units_{units} {}

    // Where, in units, the count code points from start begin and end. Nothing when start, or any of the count, is
    // past the end.
    template <IndexBase Base>
    [[nodiscard]] constexpr std::optional<std::pair<std::size_t, std::size_t>> unit_span(CodePointPosition<Base> start, std::size_t count) const noexcept {
        const auto skipped = start.skipped();
        if (!skipped) return std::nullopt;
        const auto begin = offset_of<E>(units_, *skipped);
        if (!begin) return std::nullopt;
        const auto length = offset_of<E>(units_.substr(*begin), count);
        if (!length) return std::nullopt;
        return std::pair{*begin, *begin + *length};
    }
    units_view units_{};
};

// Text that is known at compile time, checked then, and lives as long as the program: what "text"_js makes. A String
// is made from one without asking (String<> s = "text"_js;); from any other view, only when named.
template <Encoding E = Utf8>
class Literal {
public:
    using unit = typename E::unit;

    template <std::size_t N>
    consteval Literal(const unit (&literal)[N]) noexcept : view_{detail::checked_literal<E>(literal)} {}

    // Units known while compiling, checked then. Being consteval, it takes only a constant expression, and a constant
    // string_view can only point into storage that lives as long as the program.
    consteval explicit Literal(std::basic_string_view<unit> units) noexcept : view_{detail::checked_literal<E>(units)} {}

    [[nodiscard]] constexpr StringView<E> view() const noexcept { return view_; }
    constexpr operator StringView<E>() const noexcept { return view_; }

private:
    StringView<E> view_{};
};

namespace literals {

// "text"_js: a UTF-8 literal, checked while compiling.
[[nodiscard]] consteval Literal<Utf8> operator""_js(const char* text, std::size_t size) {
    return Literal<Utf8>{std::string_view{text, size}};
}

}  // namespace literals

}  // namespace jstr::inline v001

template <jstr::Encoding E>
struct std::hash<jstr::StringView<E>> {
    [[nodiscard]] std::size_t operator()(jstr::StringView<E> text) const noexcept {
        return std::hash<std::basic_string_view<typename E::unit>>{}(text.units());
    }
};
