// File: include/jString/v001/string.hpp
// Purpose: String<T>: text it owns, always valid, kept as Type T says (see type.hpp; the Type must be named). Made only
//          from text already known to be valid (a "text"_js literal, or a StringView, by name), from units through
//          from(), which says where they first go wrong, through from_lossy(), which puts U+FFFD where they do, or from
//          text of another Type through convert<To>().
//
//          Short text (up to InlineBytes bytes, 64 unless named: one cache line) is held inside the String, and longer
//          text on the heap in a std::vector. Where it is, is worked out each time, never kept as a pointer: nothing in
//          a String points into the String itself, so no compiler can take a moved or destroyed String's pointer for one
//          into the stack (g++ 15's -Wdangling-pointer did, with std::string's small-string buffer).
//
//          FixedString<T, N>: at most N code points, all inside itself. No heap and no destructor, so a block of them can
//          come from zeroed memory and never be constructed or destroyed.
//
//          Promises a caller can build on:
//            - a String moved from is empty
//            - a view into a String is good until the String is changed, moved from or destroyed: short text is inside
//              the String, and moves with it
//            - s.append(s) and s += s double s
//            - to_c_string() gives nothing, rather than a shortened string, when the text holds U+0000
//            - text of one Type never becomes another without convert<To>(), and convert<Type::Ascii>() says which code
//              point would not fit, rather than losing it (convert_lossy<Type::Ascii>() makes it '?', by name)
#pragma once

#include <algorithm>
#include <array>
#include <compare>
#include <concepts>
#include <cstddef>
#include <expected>
#include <format>
#include <optional>
#include <ostream>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "cells.hpp"
#include "encoding.hpp"
#include "string_view.hpp"
#include "type.hpp"

namespace jstr::inline v001 {

template <Type T, std::size_t InlineBytes>
class String {
public:
    static constexpr Type type = T;
    using unit = detail::unit_of<T>;
    // What from() takes: units of T's encoding, packed (for Ascii, bytes that must all be ASCII).
    using units_view = std::basic_string_view<unit>;
    // What from_lossy() takes: the same, but read as UTF-8 in Ascii, so that é is one '?', not two.
    using lossy_view = std::basic_string_view<typename detail::lossy_source<T>::unit>;
    using packed_unit = typename StringView<T>::packed_unit;
    static constexpr std::size_t inline_units = InlineBytes / sizeof(unit);

    constexpr String() noexcept = default;

    // A literal: checked while compiling, so this is the one way in that needs no name. String<Type::UTF8Full> s = "text"_js;
    template <detail::LiteralUnits L>
        requires detail::LiteralFits<T, L>
    constexpr String(Literal<L> text) : String{text.template view<T>()} {}

    // Text someone else owns, copied. Named, so that every copy is seen: String<Type::UTF16> s{view};
    constexpr explicit String(StringView<T> text) {
        const auto from = text.kept_units();
        std::ranges::copy(from, fresh(from.size()).begin());
    }

    // Units from anywhere else, packed as files and C libraries hold them: valid text, or where it first goes wrong.
    // Checked and kept in one pass, into room for the most units they can take: inside the String when that fits, else
    // on the heap, cut back to the text once it is known.
    [[nodiscard]] static constexpr std::expected<String, EncodingError> from(units_view units) {
        String text;
        const std::size_t most = detail::room_for<T>(units.size());
        if (most <= inline_units) {
            const auto stored = detail::keep_checked<T>(units, std::span{text.inside_}.first(most));
            if (!stored) return std::unexpected(stored.error());
            text.size_ = *stored;
            return text;
        }
        text.heap_.resize(most);
        const auto stored = detail::keep_checked<T>(units, text.heap_);
        if (!stored) return std::unexpected(stored.error());
        text.size_ = *stored;
        text.heap_.resize(*stored);
        // Text whose code points take fewer units than the most leaves room it will not use: give it back when more
        // than a quarter is spare.
        if (text.heap_.capacity() - text.heap_.size() > text.heap_.size() / 4) text.heap_.shrink_to_fit();
        return text;
    }

    // Units from anywhere else, each bad sequence made U+FFFD (in Ascii, every code point above U+007F made '?'): for
    // text that must be shown whatever it holds, such as a file name.
    [[nodiscard]] static constexpr String from_lossy(lossy_view units) {
        if (auto checked = from(units)) return std::move(*checked);
        return repaired(units);
    }

    constexpr String(const String&) = default;
    // Not the default: a vector copied onto one that has room keeps that room, which would put the text on the heap
    // while it is inside. A copy of text that is inside gives up the room.
    constexpr String& operator=(const String& other) {
        if (this != &other) {
            inside_ = other.inside_;
            size_ = other.size_;
            if (other.on_heap()) {
                heap_ = other.heap_;
            } else {
                heap_ = std::vector<unit>{};
            }
        }
        return *this;
    }
    // A vector moved by its constructor is left empty, as the standard says. (std::exchange(other.heap_, {}) says the
    // same, and set off g++ 16's -Warray-bounds on a String moved into a std::unordered_set's node.)
    constexpr String(String&& other) noexcept
        : inside_{other.inside_}, heap_{std::move(other.heap_)}, size_{std::exchange(other.size_, 0)} {}
    // A vector moved by assignment is left valid, but the standard does not say empty, so it is made so.
    constexpr String& operator=(String&& other) noexcept {
        if (this != &other) {
            inside_ = other.inside_;
            heap_ = std::move(other.heap_);
            size_ = std::exchange(other.size_, 0);
            other.heap_.clear();
        }
        return *this;
    }
    constexpr ~String() = default;

    [[nodiscard]] constexpr StringView<T> view() const noexcept { return StringView<T>{kept_units()}; }
    constexpr operator StringView<T>() const noexcept { return view(); }

    [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }
    // In Ascii, UTF8Full and UTF32 a division; in UTF8Packed and UTF16 a walk of the text.
    [[nodiscard]] constexpr std::size_t count_code_points() const noexcept { return view().count_code_points(); }
    [[nodiscard]] constexpr CodePoints<T> code_points() const noexcept { return view().code_points(); }

    // These give views into this String, so a String about to be destroyed has none to give: calling them on a
    // temporary does not compile.
    template <IndexBase Base>
    [[nodiscard]] constexpr std::optional<StringView<T>> at(CodePointPosition<Base> position) const& noexcept {
        return view().at(position);
    }
    template <IndexBase Base>
    std::optional<StringView<T>> at(CodePointPosition<Base>) const&& = delete;
    template <IndexBase Base>
    [[nodiscard]] constexpr std::optional<StringView<T>> substring(CodePointPosition<Base> start, CodePointCount count) const& noexcept {
        return view().substring(start, count);
    }
    template <IndexBase Base>
    std::optional<StringView<T>> substring(CodePointPosition<Base>, CodePointCount) const&& = delete;
    template <IndexBase Base>
    [[nodiscard]] constexpr std::optional<StringView<T>> substring(CodePointPosition<Base> start) const& noexcept {
        return view().substring(start);
    }
    template <IndexBase Base>
    std::optional<StringView<T>> substring(CodePointPosition<Base>) const&& = delete;
    [[nodiscard]] constexpr std::optional<std::pair<StringView<T>, StringView<T>>> split_once(StringView<T> separator) const& noexcept {
        return view().split_once(separator);
    }
    std::optional<std::pair<StringView<T>, StringView<T>>> split_once(StringView<T>) const&& = delete;

    // A new String; this one is not changed.
    template <IndexBase Base>
    [[nodiscard]] constexpr std::optional<String> replace(CodePointPosition<Base> start, CodePointCount count, StringView<T> with) const {
        return replaced(view(), start, count, with);
    }

    [[nodiscard]] constexpr bool starts_with(StringView<T> prefix) const noexcept { return view().starts_with(prefix); }
    [[nodiscard]] constexpr bool ends_with(StringView<T> suffix) const noexcept { return view().ends_with(suffix); }
    [[nodiscard]] constexpr bool contains(StringView<T> part) const noexcept { return view().contains(part); }

    // Text going out: see StringView.
    [[nodiscard]] constexpr std::size_t size_packed() const noexcept { return view().size_packed(); }
    [[nodiscard]] constexpr std::optional<std::size_t> write_packed(std::span<packed_unit> out) const noexcept { return view().write_packed(out); }
    [[nodiscard]] constexpr std::vector<packed_unit> to_packed() const { return view().to_packed(); }
    [[nodiscard]] constexpr std::optional<std::vector<packed_unit>> to_c_string() const { return view().to_c_string(); }
    [[nodiscard]] constexpr std::size_t size_utf8() const noexcept { return view().size_utf8(); }
    [[nodiscard]] constexpr std::optional<std::size_t> write_utf8(std::span<char> out) const noexcept { return view().write_utf8(out); }
    [[nodiscard]] constexpr std::vector<char> to_utf8() const { return view().to_utf8(); }

    // Room for this many code points of any width without making room again: for a String about to be built up in
    // pieces.
    constexpr void reserve_code_points(std::size_t code_points) { reserve_units(code_points * detail::max_kept<T>); }

    // Adds text at the end. The text may be this String's own: it is read in full before anything it is in is let go.
    // When there is no room, the room is doubled, so a String built by many appends is copied about once, not once per
    // append. The text added is valid and kept as this String keeps text, so nothing is checked: it is copied.
    constexpr String& append(StringView<T> more) {
        const detail::kept<T> added = more.kept_units();
        if (added.empty()) return *this;
        const std::size_t total = size_ + added.size();
        if (total > room()) {
            std::vector<unit> made;
            made.reserve(std::max(total, 2 * room()));
            const detail::kept<T> mine = kept_units();
            made.insert(made.end(), mine.begin(), mine.end());
            made.insert(made.end(), added.begin(), added.end());
            heap_ = std::move(made);
        } else if (!on_heap()) {
            // Nothing moves. more, if it is this String's own text, lies before where the new text goes, so nothing it
            // holds is written over. A String with no room inside never gets here (more is not empty, so it did not
            // fit), and its empty array has no storage to copy into: the copy is not even compiled for it.
            if constexpr (inline_units > 0) std::ranges::copy(added, std::span{inside_}.subspan(size_).begin());
        } else {
            // There is room, so the vector's storage does not move, and more, if it is this String's own text, stays
            // where it is while it is copied.
            const std::size_t before = heap_.size();
            heap_.resize(total);
            std::ranges::copy(added, std::span{heap_}.subspan(before).begin());
        }
        size_ = total;
        return *this;
    }
    constexpr String& operator+=(StringView<T> more) { return append(more); }

    // String with String is its own pair, or a == b would match the String-with-view pair both ways round, and C++20
    // calls that ambiguous. A template, so that it takes a String only, and a literal still goes to the pair below.
    template <std::same_as<String> Other>
    [[nodiscard]] friend constexpr bool operator==(const String& a, const Other& b) noexcept { return a.view() == b.view(); }
    template <std::same_as<String> Other>
    [[nodiscard]] friend constexpr std::strong_ordering operator<=>(const String& a, const Other& b) noexcept { return a.view() <=> b.view(); }
    [[nodiscard]] friend constexpr bool operator==(const String& a, StringView<T> b) noexcept { return a.view() == b; }
    [[nodiscard]] friend constexpr std::strong_ordering operator<=>(const String& a, StringView<T> b) noexcept { return a.view() <=> b; }
    template <std::same_as<char32_t> C>
    [[nodiscard]] friend constexpr bool operator==(const String& text, C code_point) noexcept { return text.view() == code_point; }

    friend std::ostream& operator<<(std::ostream& out, const String& text) { return out << text.view(); }

private:
    friend StringView<T>;
    friend detail::Access;

    // The text is on the heap once the vector has room of its own: a vector moved from, cleared or never used has none,
    // or none that holds text.
    [[nodiscard]] constexpr bool on_heap() const noexcept { return heap_.capacity() != 0; }

    // How many units fit where the text is now.
    [[nodiscard]] constexpr std::size_t room() const noexcept { return on_heap() ? heap_.capacity() : inline_units; }

    // The text's units. On the heap, the vector holds exactly the text's units.
    [[nodiscard]] constexpr detail::kept<T> kept_units() const noexcept {
        if (on_heap()) return detail::kept<T>{heap_};
        return detail::kept<T>{inside_}.first(size_);
    }

    // Room for this many units, on the heap when they do not fit where the text is.
    constexpr void reserve_units(std::size_t units) {
        if (units <= room()) return;
        std::vector<unit> made;
        made.reserve(units);
        const detail::kept<T> mine = kept_units();
        made.insert(made.end(), mine.begin(), mine.end());
        heap_ = std::move(made);
    }

    // all with count code points from start replaced by with: for replace(), here and in StringView.
    template <IndexBase Base>
    [[nodiscard]] static constexpr std::optional<String> replaced(StringView<T> all, CodePointPosition<Base> start, CodePointCount count,
                                                                  StringView<T> with) {
        const detail::kept<T> units = all.kept_units();
        const std::size_t begin = all.begin_of(start);
        if (begin == detail::nowhere) return std::nullopt;
        const std::size_t length = detail::offset_of<T>(units.subspan(begin), count.value);
        if (length == detail::nowhere) return std::nullopt;
        String made;
        made.reserve_units(units.size() - length + with.kept_units().size());
        made += StringView<T>{units.first(begin)};
        made += with;
        made += StringView<T>{units.subspan(begin + length)};
        return made;
    }

    // Units with bad sequences in them, each made U+FFFD. Its own function, returning one String only, so the copy is
    // left out: a function that can also return another object (from_lossy's valid text) cannot leave it out.
    [[nodiscard]] static constexpr String repaired(lossy_view units) {
        String text;
        static_cast<void>(detail::keep_lossy<T>(units, text.fresh(detail::lossy_size<T>(units))));
        return text;
    }

    // Only on a String just made: room for size units, inside or on the heap, for the caller to fill.
    [[nodiscard]] constexpr std::span<unit> fresh(std::size_t size) {
        size_ = size;
        if (size <= inline_units) return std::span{inside_}.first(size);
        heap_.resize(size);
        return heap_;
    }

    // Text of up to inline_units units lives here, and the vector has no room. Longer text lives in heap_, which then
    // holds exactly its units. Where the text is, is worked out from heap_ each time it is needed, and never kept as a
    // pointer, so a String never points into itself.
    std::array<unit, inline_units> inside_{};
    std::vector<unit> heap_{};
    std::size_t size_{};
};

// Text of at most MaxCodePoints code points, held entirely inside itself: no heap, no destructor, no pointer. It is
// trivially copyable and destructible, and all zeros is the empty text, so a block of them can come straight from
// zeroed memory and never be constructed or destroyed: what a store of many rows wants. Room is kept for the widest
// code points; only the first size units are text, and the units after them are never read, so they need not be
// cleared.
template <Type T, std::size_t MaxCodePoints>
class FixedString {
public:
    static constexpr Type type = T;
    using unit = detail::unit_of<T>;
    using lossy_view = std::basic_string_view<typename detail::lossy_source<T>::unit>;
    static constexpr std::size_t max_code_points = MaxCodePoints;

    constexpr FixedString() noexcept = default;

    // Units from anywhere, each bad sequence made U+FFFD (in Ascii, each code point above U+007F made '?') and the
    // text cut after MaxCodePoints code points: for text that must be kept whatever it holds, such as an event's
    // payload. Gives how many code points were kept.
    constexpr std::size_t assign_lossy(lossy_view units) noexcept {
        const detail::Kept kept = detail::keep_lossy<T>(units, std::span{units_}, MaxCodePoints);
        size_ = kept.units;
        return kept.code_points;
    }
    [[nodiscard]] static constexpr FixedString from_lossy(lossy_view units) noexcept {
        FixedString text;
        static_cast<void>(text.assign_lossy(units));
        return text;
    }

    // Valid text, copied whole: nothing when it holds more than MaxCodePoints code points.
    [[nodiscard]] static constexpr std::optional<FixedString> from(StringView<T> text) noexcept {
        if (text.count_code_points() > MaxCodePoints) return std::nullopt;
        FixedString made;
        const auto units = text.kept_units();
        std::ranges::copy(units, made.units_.begin());
        made.size_ = units.size();
        return made;
    }

    constexpr void clear() noexcept { size_ = 0; }

    [[nodiscard]] constexpr StringView<T> view() const noexcept { return StringView<T>{detail::kept<T>{units_}.first(size_)}; }
    constexpr operator StringView<T>() const noexcept { return view(); }

    [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] constexpr std::size_t count_code_points() const noexcept { return view().count_code_points(); }

    [[nodiscard]] friend constexpr bool operator==(const FixedString& a, StringView<T> b) noexcept { return a.view() == b; }
    [[nodiscard]] friend constexpr std::strong_ordering operator<=>(const FixedString& a, StringView<T> b) noexcept { return a.view() <=> b; }

    friend std::ostream& operator<<(std::ostream& out, const FixedString& text) { return out << text.view(); }

private:
    std::array<unit, MaxCodePoints * detail::max_kept<T>> units_{};
    std::size_t size_{};
};

// Why text could not be converted: the code point, and its index, that the Type converted to cannot hold. Only Ascii
// cannot hold every code point.
struct ConversionError {
    std::size_t index;
    char32_t code_point;

    friend constexpr bool operator==(const ConversionError&, const ConversionError&) noexcept = default;
};

namespace detail {

struct Access {
    template <Type To, std::size_t InlineBytes, Type From>
    [[nodiscard]] static constexpr String<To, InlineBytes> converted(StringView<From> text) {
        const kept<From> units = text.kept_units();
        String<To, InlineBytes> made;
        convert<From, To>(units, made.fresh(converted_size<From, To>(units)));
        return made;
    }

    // Every code point above U+007F made '?'.
    template <std::size_t InlineBytes, Type From>
    [[nodiscard]] static constexpr String<Type::Ascii, InlineBytes> converted_lossy(StringView<From> text) {
        const kept<From> units = text.kept_units();
        String<Type::Ascii, InlineBytes> made;
        const std::span<char> out = made.fresh(count_of<From>(units));
        std::size_t to = 0;
        for (std::size_t at = 0; at < units.size(); at += length_at<From>(units, at)) {
            out[to] = encode_kept<Type::Ascii>(decode_at<From>(units, at)).units[0];
            ++to;
        }
        return made;
    }

    template <Type T>
    [[nodiscard]] static constexpr kept<T> units_of(StringView<T> text) noexcept {
        return text.kept_units();
    }
};

template <Type T, std::size_t N>
[[nodiscard]] constexpr String<T, N> joined(StringView<T> a, StringView<T> b) {
    String<T, N> made{a};
    made += b;
    return made;
}

}  // namespace detail

// Text of one Type as another, by name. To every Type but Ascii it cannot fail: each holds every code point. To Ascii
// it gives the first code point above U+007F instead, and convert_lossy<Type::Ascii>() makes each one '?'.
template <Type To, std::size_t InlineBytes = default_inline_bytes, Type From>
[[nodiscard]] constexpr auto convert(StringView<From> text) {
    if constexpr (To == Type::Ascii && From != Type::Ascii) {
        using result = std::expected<String<To, InlineBytes>, ConversionError>;
        if (const auto bad = detail::first_not_ascii<From>(detail::Access::units_of(text))) return result{std::unexpect, bad->index, bad->code_point};
        return result{detail::Access::converted<To, InlineBytes>(text)};
    } else {
        return detail::Access::converted<To, InlineBytes>(text);
    }
}
template <Type To, std::size_t InlineBytes = default_inline_bytes, Type From, std::size_t N>
[[nodiscard]] constexpr auto convert(const String<From, N>& text) {
    return convert<To, InlineBytes>(text.view());
}
template <Type To, std::size_t InlineBytes = default_inline_bytes, Type From, std::size_t N>
[[nodiscard]] constexpr auto convert(const FixedString<From, N>& text) {
    return convert<To, InlineBytes>(text.view());
}

template <Type To, std::size_t InlineBytes = default_inline_bytes, Type From>
    requires(To == Type::Ascii)
[[nodiscard]] constexpr String<To, InlineBytes> convert_lossy(StringView<From> text) {
    return detail::Access::converted_lossy<InlineBytes>(text);
}
template <Type To, std::size_t InlineBytes = default_inline_bytes, Type From, std::size_t N>
    requires(To == Type::Ascii)
[[nodiscard]] constexpr String<To, InlineBytes> convert_lossy(const String<From, N>& text) {
    return convert_lossy<To, InlineBytes>(text.view());
}

// Joining makes a new String. Each form takes text of one Type only, so two Types cannot be joined by mistake.
template <Type T, std::size_t N>
[[nodiscard]] constexpr String<T, N> operator+(const String<T, N>& a, const String<T, N>& b) {
    return detail::joined<T, N>(a, b);
}
template <Type T, std::size_t N>
[[nodiscard]] constexpr String<T, N> operator+(const String<T, N>& a, std::type_identity_t<StringView<T>> b) {
    return detail::joined<T, N>(a, b);
}
template <Type T, std::size_t N>
[[nodiscard]] constexpr String<T, N> operator+(std::type_identity_t<StringView<T>> a, const String<T, N>& b) {
    return detail::joined<T, N>(a, b);
}
template <Type T>
[[nodiscard]] constexpr String<T> operator+(StringView<T> a, std::type_identity_t<StringView<T>> b) {
    return detail::joined<T, default_inline_bytes>(a, b);
}

namespace detail {

// Whether no std:: string view, span or pointer can be made from a jString's text of Type T: its units are not handed
// out, so a reader that expects packed text cannot be given kept text (UTF8Full's cells) by mistake.
template <class Text>
inline constexpr bool sealed_one =
    !std::is_constructible_v<std::string_view, const Text&> && !std::is_constructible_v<std::u16string_view, const Text&> &&
    !std::is_constructible_v<std::u32string_view, const Text&> && !std::is_constructible_v<std::span<const char>, const Text&> &&
    !std::is_constructible_v<std::span<const char16_t>, const Text&> && !std::is_constructible_v<std::span<const char32_t>, const Text&> &&
    !std::is_constructible_v<const char*, const Text&> && !std::is_constructible_v<const unsigned char*, const Text&>;
template <Type T>
inline constexpr bool sealed = sealed_one<String<T>> && sealed_one<StringView<T>> && sealed_one<FixedString<T, 1>>;

}  // namespace detail

// Checked here, so that every build of anything that includes jString proves it.
static_assert(detail::sealed<Type::Ascii> && detail::sealed<Type::UTF8Packed> && detail::sealed<Type::UTF8Full> && detail::sealed<Type::UTF16> &&
              detail::sealed<Type::UTF32>);

}  // namespace jstr::inline v001

template <jstr::Type T, std::size_t N>
struct std::hash<jstr::String<T, N>> {
    [[nodiscard]] std::size_t operator()(const jstr::String<T, N>& text) const noexcept { return std::hash<jstr::StringView<T>>{}(text.view()); }
};

// std::format("{}", text), with every option a std::string_view takes, given the text as UTF-8 whatever its Type.
template <jstr::Type T>
struct std::formatter<jstr::StringView<T>, char> : std::formatter<std::string_view, char> {
    template <class Context>
    auto format(jstr::StringView<T> text, Context& context) const {
        const std::vector<char> utf8 = text.to_utf8();
        return std::formatter<std::string_view, char>::format(std::string_view{utf8.begin(), utf8.end()}, context);
    }
};

template <jstr::Type T, std::size_t N>
struct std::formatter<jstr::String<T, N>, char> : std::formatter<jstr::StringView<T>, char> {
    template <class Context>
    auto format(const jstr::String<T, N>& text, Context& context) const {
        return std::formatter<jstr::StringView<T>, char>::format(text.view(), context);
    }
};

template <jstr::Type T, std::size_t N>
struct std::formatter<jstr::FixedString<T, N>, char> : std::formatter<jstr::StringView<T>, char> {
    template <class Context>
    auto format(const jstr::FixedString<T, N>& text, Context& context) const {
        return std::formatter<jstr::StringView<T>, char>::format(text.view(), context);
    }
};
