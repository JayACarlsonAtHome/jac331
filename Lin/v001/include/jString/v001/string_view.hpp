// File: include/jString/v001/string_view.hpp
// Purpose: StringView<T>: text someone else owns, always valid, kept as Type T says (see type.hpp). There is no way to
//          make one from units that were not checked: it is had only from a String, a FixedString, another StringView,
//          or a "text"_js literal, whose units the compiler makes and checks. Text from outside comes in through
//          String::from(), which checks it.
//
//          It has no operator[] and no offsets in units, so nothing can cut a code point in two. It never hands out the
//          units it keeps: text goes out only by name, packed as files and C libraries read it (to_packed(),
//          write_packed(), to_c_string()), or as UTF-8 whatever its Type (to_utf8(), write_utf8()). Text of one Type
//          never meets text of another without being converted by name (convert<To>(), in string.hpp).
// Modified 2026-11-23 by Claude Opus 5.5: [[clang::lifetimebound]] where clang++ 23 asks for it.
#pragma once

#include <algorithm>
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
#include "type.hpp"

namespace jString::inline v001 {

// How many bytes a String holds inside itself before its text goes to the heap: one cache line.
inline constexpr std::size_t default_inline_bytes = 64;

template <Type T, std::size_t InlineBytes = default_inline_bytes>
class String;

template <Type T, std::size_t MaxCodePoints>
class FixedString;

template <Type T>
class StringView;

namespace detail {

// Never defined, and not constexpr: a constant evaluation that reaches one stops there, and the compiler names it.
void the_literal_is_not_valid_text();

// A string literal as a template argument, so that its units can be made while compiling and kept for the whole run.
template <std::size_t N>
struct LiteralUnits {
    consteval LiteralUnits(const char (&literal)[N]) noexcept { std::ranges::copy(literal, units.begin()); }
    std::array<char, N> units{};  // with the literal's terminating zero, which is not text

    [[nodiscard]] consteval std::string_view text() const noexcept { return std::string_view{units.begin(), units.end()}.substr(0, N - 1); }
};

// Made from a string literal, its size taken from the literal.
template <std::size_t N>
LiteralUnits(const char (&)[N]) -> LiteralUnits<N>;

struct Access;

}  // namespace detail

template <detail::LiteralUnits L>
class Literal;

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
        if (value == 0 && Base == IndexBase::one_based) return std::nullopt;
        return skipped_or_nowhere();
    }

    // The same, as a plain number: no position is the largest size_t, which no text is that long. For jString's own
    // hot paths (see detail::nowhere).
    [[nodiscard]] constexpr std::size_t skipped_or_nowhere() const noexcept {
        if constexpr (Base == IndexBase::zero_based) {
            return value;
        } else {
            return value - 1;  // 0 wraps to the largest size_t: nowhere
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
template <Type T>
class CodePoints : public std::ranges::view_interface<CodePoints<T>> {
public:
    using kept = detail::kept<T>;

    class iterator {
    public:
        using value_type = char32_t;
        using difference_type = std::ptrdiff_t;
        using iterator_concept = std::forward_iterator_tag;

        constexpr iterator() noexcept = default;

        // Valid text: decoded with no checking.
        [[nodiscard]] constexpr char32_t operator*() const noexcept { return detail::decode_at<T>(rest_, 0); }
        constexpr iterator& operator++() noexcept [[clang::lifetimebound]] {
            rest_ = rest_.subspan(detail::length_at<T>(rest_, 0));
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
        constexpr explicit iterator(kept rest [[clang::lifetimebound]]) noexcept : rest_{rest} {}
        kept rest_{};
    };

    [[nodiscard]] constexpr iterator begin() const noexcept { return iterator{text_}; }
    [[nodiscard]] constexpr std::default_sentinel_t end() const noexcept { return {}; }

private:
    friend StringView<T>;
    constexpr explicit CodePoints(kept text [[clang::lifetimebound]]) noexcept : text_{text} {}
    kept text_{};
};

template <Type T>
class StringView {
public:
    static constexpr Type type = T;
    using unit = detail::unit_of<T>;
    // What text goes out as, packed: T's own units, but UTF8Full packs to UTF-8 bytes, which are char too.
    using packed_unit = detail::unit_of<detail::packed_type<T>>;

    constexpr StringView() noexcept = default;

    [[nodiscard]] constexpr bool empty() const noexcept { return units_.empty(); }
    // In Ascii, UTF8Full and UTF32 a division; in UTF8Packed and UTF16 a walk of the text.
    [[nodiscard]] constexpr std::size_t count_code_points() const noexcept { return detail::count_of<T>(units_); }
    [[nodiscard]] constexpr CodePoints<T> code_points() const noexcept { return CodePoints<T>{units_}; }

    // The one code point at a position, as text, or nothing when the text is shorter.
    template <IndexBase Base>
    [[nodiscard]] constexpr std::optional<StringView> at(CodePointPosition<Base> position) const noexcept {
        return substring(position, CodePointCount{1});
    }

    // count code points from start. Nothing unless all of them are in the text: never a shorter piece. A start just
    // past the last code point, with a count of 0, is the empty text at the end.
    template <IndexBase Base>
    [[nodiscard]] constexpr std::optional<StringView> substring(CodePointPosition<Base> start, CodePointCount count) const noexcept {
        const std::size_t begin = begin_of(start);
        if (begin == detail::nowhere) return std::nullopt;
        const detail::kept<T> rest = units_.subspan(begin);
        const std::size_t length = detail::offset_of<T>(rest, count.value);
        if (length == detail::nowhere) return std::nullopt;
        return StringView{rest.first(length)};
    }

    // From start to the end.
    template <IndexBase Base>
    [[nodiscard]] constexpr std::optional<StringView> substring(CodePointPosition<Base> start) const noexcept {
        const std::size_t begin = begin_of(start);
        if (begin == detail::nowhere) return std::nullopt;
        return StringView{units_.subspan(begin)};
    }

    // A new String: this text with count code points from start replaced by with. Nothing unless all of them are in
    // the text. This text is not changed. (Needs string.hpp, which jstring.hpp includes.)
    template <IndexBase Base>
    [[nodiscard]] constexpr std::optional<String<T>> replace(CodePointPosition<Base> start, CodePointCount count, StringView with) const {
        return String<T>::replaced(*this, start, count, with);
    }

    // Kept text starts a code point where a piece of valid text matches it, so comparing units answers these exactly.
    [[nodiscard]] constexpr bool starts_with(StringView prefix) const noexcept { return view().starts_with(prefix.view()); }
    [[nodiscard]] constexpr bool ends_with(StringView suffix) const noexcept { return view().ends_with(suffix.view()); }
    [[nodiscard]] constexpr bool contains(StringView part) const noexcept { return detail::find<T>(units_, part.units_).has_value(); }

    // The text before and after the first separator. Nothing when it is not there, or when the separator is empty,
    // which is never the separator anyone meant.
    [[nodiscard]] constexpr std::optional<std::pair<StringView, StringView>> split_once(StringView separator) const noexcept {
        if (separator.empty()) return std::nullopt;
        const auto at = detail::find<T>(units_, separator.units_);
        if (!at) return std::nullopt;
        return std::pair{StringView{units_.first(*at)}, StringView{units_.subspan(*at + separator.units_.size())}};
    }

    // Code point order, on every machine.
    // Equal is the same units, which compilers compare as one block of memory; char_traits compares char16_t and
    // char32_t one unit at a time.
    [[nodiscard]] friend constexpr bool operator==(StringView a, StringView b) noexcept { return std::ranges::equal(a.units_, b.units_); }
    [[nodiscard]] friend constexpr std::strong_ordering operator<=>(StringView a, StringView b) noexcept {
        return detail::compare<T>(a.units_, b.units_);
    }

    // Whether the text is exactly this one code point. Only a char32_t: view == 7 must not compile.
    template <std::same_as<char32_t> C>
    [[nodiscard]] friend constexpr bool operator==(StringView text, C code_point) noexcept {
        return !text.empty() && detail::length_at<T>(text.units_, 0) == text.units_.size() && detail::decode_at<T>(text.units_, 0) == code_point;
    }

    // --- Text going out, by name: the one way out of jString ---

    // How many units the text takes packed, as files and C libraries hold it (in Ascii, UTF8Packed and UTF8Full: bytes;
    // in UTF16 and UTF32, units in this machine's byte order).
    [[nodiscard]] constexpr std::size_t size_packed() const noexcept { return detail::converted_size<T, detail::packed_type<T>>(units_); }

    // The text packed into out, with no terminating zero. Gives how many units were written, or nothing, with nothing
    // written, when out is too small.
    [[nodiscard]] constexpr std::optional<std::size_t> write_packed(std::span<packed_unit> out) const noexcept { return write<detail::packed_type<T>>(out); }

    // The text packed into units of its own, with no terminating zero.
    [[nodiscard]] constexpr std::vector<packed_unit> to_packed() const { return made<detail::packed_type<T>>(0); }

    // For a C library: the text packed, then a terminating zero. Nothing when the text holds U+0000, because C would
    // stop there and quietly lose the rest.
    [[nodiscard]] constexpr std::optional<std::vector<packed_unit>> to_c_string() const {
        if (detail::find<T>(units_, detail::encode_kept<T>(U'\0').view()).has_value()) return std::nullopt;
        return made<detail::packed_type<T>>(1);
    }

    // The text as UTF-8, whatever its Type: what files, terminals and most C libraries read.
    [[nodiscard]] constexpr std::size_t size_utf8() const noexcept { return detail::converted_size<T, Type::UTF8Packed>(units_); }
    [[nodiscard]] constexpr std::optional<std::size_t> write_utf8(std::span<char> out) const noexcept { return write<Type::UTF8Packed>(out); }
    [[nodiscard]] constexpr std::vector<char> to_utf8() const { return made<Type::UTF8Packed>(0); }

    // Printed as UTF-8, a piece at a time, with nothing made on the heap.
    friend std::ostream& operator<<(std::ostream& out, StringView text) {
        if constexpr (detail::copies_to<T, Type::UTF8Packed>) {
            return out.write(text.units_.data(), static_cast<std::streamsize>(text.units_.size()));
        } else {
            std::array<char, 256> piece{};
            std::size_t filled = 0;
            for (std::size_t at = 0; at < text.units_.size(); at += detail::length_at<T>(text.units_, at)) {
                const auto one = detail::encode_kept<Type::UTF8Packed>(detail::decode_at<T>(text.units_, at));
                if (filled + one.size > piece.size()) {
                    out.write(piece.data(), static_cast<std::streamsize>(filled));
                    filled = 0;
                }
                std::ranges::copy(one.view(), std::span{piece}.subspan(filled).begin());
                filled += one.size;
            }
            return out.write(piece.data(), static_cast<std::streamsize>(filled));
        }
    }

private:
    template <Type, std::size_t>
    friend class String;
    template <Type, std::size_t>
    friend class FixedString;
    template <detail::LiteralUnits>
    friend class Literal;
    friend detail::Access;
    friend struct std::hash<StringView>;

    // Only for units of valid text, kept as T keeps it.
    constexpr explicit StringView(detail::kept<T> units [[clang::lifetimebound]]) noexcept : units_{units} {}

    [[nodiscard]] constexpr detail::units_view<T> view() const noexcept { return detail::view_of<T>(units_); }

    // Where start begins, in units, or nowhere.
    template <IndexBase Base>
    [[nodiscard]] constexpr std::size_t begin_of(CodePointPosition<Base> start) const noexcept {
        const std::size_t first = start.skipped_or_nowhere();
        if (first == detail::nowhere) return detail::nowhere;
        return detail::offset_of<T>(units_, first);
    }
    [[nodiscard]] constexpr detail::kept<T> kept_units() const noexcept { return units_; }

    template <Type To>
    [[nodiscard]] constexpr std::optional<std::size_t> write(std::span<detail::unit_of<To>> out) const noexcept {
        const std::size_t size = detail::converted_size<T, To>(units_);
        if (out.size() < size) return std::nullopt;
        detail::convert<T, To>(units_, out.first(size));
        return size;
    }

    // The text converted to To, with extra zero units after it. Empty text converts nothing: its vector may have no
    // storage at all (a null pointer), and g++ cannot see that a copy of nothing into it reads nothing.
    template <Type To>
    [[nodiscard]] constexpr std::vector<detail::unit_of<To>> made(std::size_t extra) const {
        const std::size_t size = detail::converted_size<T, To>(units_);
        std::vector<detail::unit_of<To>> out(size + extra);
        if (size != 0) detail::convert<T, To>(units_, std::span{out}.first(size));
        return out;
    }

    detail::kept<T> units_{};
};

namespace detail {

// How many code points a literal holds; a literal that is not valid UTF-8 stops the compile here.
template <LiteralUnits L>
[[nodiscard]] consteval std::size_t literal_count() {
    const auto checked = validate<Utf8>(L.text());
    if (!checked) the_literal_is_not_valid_text();
    return *checked;
}

template <LiteralUnits L>
inline constexpr bool literal_is_ascii = std::ranges::all_of(L.text(), [](char unit) noexcept { return value_of(unit) < 0x80U; });

// Whether a literal can be text of Type T: any literal can, but Ascii takes ASCII only.
template <Type T, LiteralUnits L>
concept LiteralFits = T != Type::Ascii || literal_is_ascii<L>;

// A literal's units kept as T keeps them, made in place while compiling (a returned array would be a copy clang++
// warns it cannot leave out).
template <Type T, std::size_t Size>
struct LiteralKept {
    consteval explicit LiteralKept(std::string_view utf8) noexcept { convert<Type::UTF8Packed, T>(kept<Type::UTF8Packed>{utf8}, std::span{units}); }
    std::array<unit_of<T>, Size> units{};
};

template <Type T, LiteralUnits L>
[[nodiscard]] consteval std::size_t literal_size() {
    static_cast<void>(literal_count<L>());
    return converted_size<Type::UTF8Packed, T>(kept<Type::UTF8Packed>{L.text()});
}

// A literal's units for Type T, kept for the whole run: made only for the Types the literal is used as.
template <Type T, LiteralUnits L>
inline constexpr LiteralKept<T, literal_size<T, L>()> literal_kept{L.text()};

}  // namespace detail

// Text known at compile time, checked then, and living as long as the program: what "text"_js makes. It is text of
// every Type it fits (Ascii only when it is ASCII), made while compiling for each Type it is used as. A String is made
// from one without asking (jString::String<jString::Type::UTF16> s = "text"_js;), and a function taking a StringView takes
// one as it is. A plain "text" is not text: nothing checked it.
template <detail::LiteralUnits L>
class Literal {
public:
    template <Type T>
        requires detail::LiteralFits<T, L>
    [[nodiscard]] constexpr StringView<T> view() const noexcept {
        return StringView<T>{detail::kept<T>{detail::literal_kept<T, L>.units}};
    }
    template <Type T>
        requires detail::LiteralFits<T, L>
    constexpr operator StringView<T>() const noexcept {
        return view<T>();
    }
};

// A literal compared with a literal, as text: "a"_js == "a"_js.
template <detail::LiteralUnits A, detail::LiteralUnits B>
[[nodiscard]] constexpr bool operator==(Literal<A>, Literal<B>) noexcept {
    return A.text() == B.text();
}

namespace literals {

// "text"_js: a UTF-8 literal, checked while compiling.
template <detail::LiteralUnits L>
[[nodiscard]] consteval Literal<L> operator""_js() noexcept {
    static_cast<void>(detail::literal_count<L>());
    return Literal<L>{};
}

}  // namespace literals

}  // namespace jString::inline v001

// The short name of a view, as jstr is of the namespace: jstr_vw<jstr::Type::UTF8Packed> is jString::StringView<jString::Type::UTF8Packed>.
template <jString::Type T> using jstr_vw = jString::StringView<T>;

template <jString::Type T>
struct std::hash<jString::StringView<T>> {
    [[nodiscard]] std::size_t operator()(jString::StringView<T> text) const noexcept { return std::hash<jString::detail::units_view<T>>{}(text.view()); }
};
