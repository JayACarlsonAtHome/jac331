// File: include/jString/v001/string.hpp
// Purpose: String<E>: text it owns, always valid in encoding E (UTF-8 unless named). Made only from text already
//          known to be valid (a "text"_js literal, or a StringView, by name), from units through from(), which says
//          where they first go wrong, or through from_lossy(), which puts U+FFFD where they do.
//
//          Every code point takes one cell of E::cell_units units (4 for UTF-8): its own units, then zeros. Code point
//          n is cell n, so at(), substring() and counting are a multiplication, with no table and no walk. Space is
//          traded for speed and simplicity: ASCII takes 4 bytes a code point, not 1.
//
//          Short text (up to InlineCells code points, 16 unless named: 64 bytes, one cache line) is held inside the
//          String, and longer text on the heap in a std::vector. Where it is, is worked out each time, never kept as a
//          pointer: nothing in a String points into the String itself, so no compiler can take a moved or destroyed
//          String's pointer for one into the stack (g++ 15's -Wdangling-pointer did, with std::string's small-string
//          buffer).
//
//          FixedString<N>: at most N code points, all inside itself. No heap and no destructor, so a block of them can
//          come from zeroed memory and never be constructed or destroyed.
//
//          Promises a caller can build on:
//            - a String moved from is empty
//            - a view into a String is good until the String is changed, moved from or destroyed: short text is inside
//              the String, and moves with it
//            - s.append(s) and s += s double s
//            - to_c_string() gives nothing, rather than a shortened string, when the text holds a zero
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

namespace jstr::inline v001 {

template <Encoding E, std::size_t InlineCells>
class String {
public:
    using encoding = E;
    using unit = typename E::unit;
    using units_view = std::basic_string_view<unit>;
    static constexpr std::size_t cell_units = E::cell_units;
    static constexpr std::size_t inline_cells = InlineCells;

    constexpr String() noexcept = default;

    // A literal: checked while compiling, so this is the one way in that needs no name. String<> s = "text"_js;
    constexpr String(Literal<E> text) : String{text.view()} {}

    // Text someone else owns, copied. Named, so that every copy is seen: String<> s{view};
    constexpr explicit String(StringView<E> text) { std::ranges::copy(text.cells(), fresh(text.count_code_points()).begin()); }

    // Units from anywhere else, packed as files and C libraries hold them: valid text, or where it first goes wrong.
    // Checked and spread into cells in one pass, into room for as many code points as there are units (the most there
    // can be): inside the String when that fits, else on the heap, cut back to the text once it is known.
    [[nodiscard]] static constexpr std::expected<String, EncodingError> from(units_view units) {
        String text;
        if (units.size() <= InlineCells) {
            const auto counted = detail::spread_checked<E>(units, std::span{text.inside_});
            if (!counted) return std::unexpected(counted.error());
            text.count_ = *counted;
            return text;
        }
        text.heap_.resize(units.size() * cell_units);
        const auto counted = detail::spread_checked<E>(units, text.heap_);
        if (!counted) return std::unexpected(counted.error());
        text.count_ = *counted;
        text.heap_.resize(*counted * cell_units);
        // Text of wide code points leaves room it will not use: give back the room when more than a quarter is spare.
        if (text.heap_.capacity() - text.heap_.size() > text.heap_.size() / 4) text.heap_.shrink_to_fit();
        return text;
    }

    // Units from anywhere else, each bad sequence made U+FFFD: for text that must be shown whatever it holds, such as
    // a file name.
    [[nodiscard]] static constexpr String from_lossy(units_view units) {
        if (auto checked = from(units)) return std::move(*checked);
        return repaired(units);
    }

    constexpr String(const String&) = default;
    // Not the default: a vector copied onto one that has room keeps that room, which would put the text on the heap
    // while it is inside. A copy of text that is inside gives up the room.
    constexpr String& operator=(const String& other) {
        if (this != &other) {
            inside_ = other.inside_;
            count_ = other.count_;
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
        : inside_{other.inside_}, heap_{std::move(other.heap_)}, count_{std::exchange(other.count_, 0)} {}
    // A vector moved by assignment is left valid, but the standard does not say empty, so it is made so.
    constexpr String& operator=(String&& other) noexcept {
        if (this != &other) {
            inside_ = other.inside_;
            heap_ = std::move(other.heap_);
            count_ = std::exchange(other.count_, 0);
            other.heap_.clear();
        }
        return *this;
    }
    constexpr ~String() = default;

    [[nodiscard]] constexpr StringView<E> view() const noexcept { return StringView<E>{cells()}; }
    constexpr operator StringView<E>() const noexcept { return view(); }

    [[nodiscard]] constexpr bool empty() const noexcept { return count_ == 0; }
    [[nodiscard]] constexpr std::size_t count_code_points() const noexcept { return count_; }
    [[nodiscard]] constexpr CodePoints<E> code_points() const noexcept { return view().code_points(); }

    // These give views into this String, so a String about to be destroyed has none to give: calling them on a
    // temporary does not compile.
    template <IndexBase Base>
    [[nodiscard]] constexpr std::optional<StringView<E>> at(CodePointPosition<Base> position) const& noexcept {
        return view().at(position);
    }
    template <IndexBase Base>
    std::optional<StringView<E>> at(CodePointPosition<Base>) const&& = delete;
    template <IndexBase Base>
    [[nodiscard]] constexpr std::optional<StringView<E>> substring(CodePointPosition<Base> start, CodePointCount count) const& noexcept {
        return view().substring(start, count);
    }
    template <IndexBase Base>
    std::optional<StringView<E>> substring(CodePointPosition<Base>, CodePointCount) const&& = delete;
    template <IndexBase Base>
    [[nodiscard]] constexpr std::optional<StringView<E>> substring(CodePointPosition<Base> start) const& noexcept {
        return view().substring(start);
    }
    template <IndexBase Base>
    std::optional<StringView<E>> substring(CodePointPosition<Base>) const&& = delete;
    [[nodiscard]] constexpr std::optional<std::pair<StringView<E>, StringView<E>>> split_once(StringView<E> separator) const& noexcept {
        return view().split_once(separator);
    }
    std::optional<std::pair<StringView<E>, StringView<E>>> split_once(StringView<E>) const&& = delete;

    // A new String; this one is not changed.
    template <IndexBase Base>
    [[nodiscard]] constexpr std::optional<String> replace(CodePointPosition<Base> start, CodePointCount count, StringView<E> with) const {
        const auto first = start.skipped();
        if (!first || *first > count_ || count.value > count_ - *first) return std::nullopt;
        const StringView<E> all = view();
        String made;
        made.reserve_code_points(count_ - count.value + with.count_code_points());
        made += *all.substring(CodePointIndex{0}, CodePointCount{*first});
        made += with;
        made += *all.substring(CodePointIndex{*first + count.value});
        return made;
    }

    [[nodiscard]] constexpr bool starts_with(StringView<E> prefix) const noexcept { return view().starts_with(prefix); }
    [[nodiscard]] constexpr bool ends_with(StringView<E> suffix) const noexcept { return view().ends_with(suffix); }
    [[nodiscard]] constexpr bool contains(StringView<E> part) const noexcept { return view().contains(part); }

    // Text going out, packed: see StringView.
    [[nodiscard]] constexpr std::size_t size_packed() const noexcept { return view().size_packed(); }
    [[nodiscard]] constexpr std::optional<std::size_t> write_packed(std::span<unit> out) const noexcept { return view().write_packed(out); }
    [[nodiscard]] constexpr std::vector<unit> to_packed() const { return view().to_packed(); }
    [[nodiscard]] constexpr std::optional<std::vector<unit>> to_c_string() const { return view().to_c_string(); }
    [[nodiscard]] constexpr std::size_t size_utf8() const noexcept
        requires std::same_as<E, Utf8>
    {
        return view().size_utf8();
    }
    [[nodiscard]] constexpr std::optional<std::size_t> write_utf8(std::span<char> out) const noexcept
        requires std::same_as<E, Utf8>
    {
        return view().write_utf8(out);
    }
    [[nodiscard]] constexpr std::vector<char> to_utf8() const
        requires std::same_as<E, Utf8>
    {
        return view().to_utf8();
    }

    // Room for this many code points without making room again: for a String about to be built up in pieces.
    constexpr void reserve_code_points(std::size_t code_points) {
        if (code_points <= room()) return;
        move_to_heap(code_points);
    }

    // Adds text at the end. The text may be this String's own: it is read in full before anything it is in is let go.
    // When there is no room, the room is doubled, so a String built by many appends is copied about once, not once per
    // append. The text added is valid and its cells are already made, so nothing is checked or counted: it is copied.
    constexpr String& append(StringView<E> more) {
        if (more.empty()) return *this;
        const std::span<const unit> added = more.cells();
        const std::size_t total = count_ + more.count_code_points();
        if (total > room()) {
            std::vector<unit> made;
            made.reserve(std::max(total, 2 * room()) * cell_units);
            const std::span<const unit> mine = cells();
            made.insert(made.end(), mine.begin(), mine.end());
            made.insert(made.end(), added.begin(), added.end());
            heap_ = std::move(made);
        } else if (!on_heap()) {
            // Nothing moves. more, if it is this String's own text, lies before where the new text goes, so nothing it
            // holds is written over. A String with no room inside never gets here (more is not empty, so it did not
            // fit), and its empty array has no storage to copy into: the copy is not even compiled for it.
            if constexpr (InlineCells > 0) std::ranges::copy(added, std::span{inside_}.subspan(count_ * cell_units).begin());
        } else {
            // There is room, so the vector's storage does not move, and more, if it is this String's own text, stays
            // where it is while it is copied.
            const std::size_t before = heap_.size();
            heap_.resize(total * cell_units);
            std::ranges::copy(added, std::span{heap_}.subspan(before).begin());
        }
        count_ = total;
        return *this;
    }
    constexpr String& operator+=(StringView<E> more) { return append(more); }

    // String with String is its own pair, or a == b would match the String-with-view pair both ways round, and C++20
    // calls that ambiguous. A template, so that it takes a String only, and a literal still goes to the pair below.
    template <std::same_as<String> Other>
    [[nodiscard]] friend constexpr bool operator==(const String& a, const Other& b) noexcept { return a.view() == b.view(); }
    template <std::same_as<String> Other>
    [[nodiscard]] friend constexpr std::strong_ordering operator<=>(const String& a, const Other& b) noexcept { return a.view() <=> b.view(); }
    [[nodiscard]] friend constexpr bool operator==(const String& a, StringView<E> b) noexcept { return a.view() == b; }
    [[nodiscard]] friend constexpr std::strong_ordering operator<=>(const String& a, StringView<E> b) noexcept { return a.view() <=> b; }
    template <std::same_as<char32_t> C>
    [[nodiscard]] friend constexpr bool operator==(const String& text, C code_point) noexcept { return text.view() == code_point; }

    friend std::ostream& operator<<(std::ostream& out, const String& text)
        requires std::same_as<unit, char>
    {
        return out << text.view();
    }

private:
    // The text is on the heap once the vector has room of its own: a vector moved from, cleared or never used has none,
    // or none that holds text.
    [[nodiscard]] constexpr bool on_heap() const noexcept { return heap_.capacity() != 0; }

    // How many code points fit where the text is now.
    [[nodiscard]] constexpr std::size_t room() const noexcept { return on_heap() ? heap_.capacity() / cell_units : InlineCells; }

    // The text's cells. On the heap, the vector holds exactly the text's cells.
    [[nodiscard]] constexpr std::span<const unit> cells() const noexcept {
        if (on_heap()) return std::span<const unit>{heap_};
        return std::span<const unit>{inside_}.first(count_ * cell_units);
    }

    // Moves the text to the heap, with room for code_points code points.
    constexpr void move_to_heap(std::size_t code_points) {
        std::vector<unit> made;
        made.reserve(code_points * cell_units);
        const std::span<const unit> mine = cells();
        made.insert(made.end(), mine.begin(), mine.end());
        heap_ = std::move(made);
    }

    // Units with bad sequences in them, each made U+FFFD. Its own function, returning one String only, so the copy is
    // left out: a function that can also return another object (from_lossy's valid text) cannot leave it out.
    [[nodiscard]] static constexpr String repaired(units_view units) {
        String text;
        static_cast<void>(detail::spread_lossy<E>(units, text.fresh(detail::lossy_count<E>(units))));
        return text;
    }

    // Only on a String just made: room for count code points, inside or on the heap, for the caller to fill.
    [[nodiscard]] constexpr std::span<unit> fresh(std::size_t count) {
        count_ = count;
        if (count <= InlineCells) return std::span{inside_}.first(count * cell_units);
        heap_.resize(count * cell_units);
        return heap_;
    }

    // Text of up to InlineCells code points lives here, and the vector has no room. Longer text lives in heap_, which
    // then holds exactly its cells. Where the text is, is worked out from heap_ each time it is needed, and never kept
    // as a pointer, so a String never points into itself: moving one copies inside_ and moves the vector's pointer, and
    // no compiler can take a pointer in a moved or destroyed String for one into the stack (g++ 15's -Wdangling-pointer
    // did, with std::string).
    std::array<unit, InlineCells * E::cell_units> inside_{};
    std::vector<unit> heap_{};
    std::size_t count_{};
};

// Text of at most MaxCodePoints code points, held entirely inside itself: no heap, no destructor, no pointer. It is
// trivially copyable and destructible, and all zeros is the empty text, so a block of them can come straight from
// zeroed memory and never be constructed or destroyed: what a store of many rows wants. Only the first count cells
// are text; the cells after them are never read, so they need not be cleared.
template <std::size_t MaxCodePoints, Encoding E>
class FixedString {
public:
    using encoding = E;
    using unit = typename E::unit;
    using units_view = std::basic_string_view<unit>;
    static constexpr std::size_t cell_units = E::cell_units;
    static constexpr std::size_t max_code_points = MaxCodePoints;

    constexpr FixedString() noexcept = default;

    // Units from anywhere, each bad sequence made U+FFFD and the text cut after MaxCodePoints code points: for text
    // that must be kept whatever it holds, such as an event's payload. Gives how many code points were kept.
    constexpr std::size_t assign_lossy(units_view units) noexcept {
        count_ = detail::spread_lossy<E>(units, std::span{cells_});
        return count_;
    }
    [[nodiscard]] static constexpr FixedString from_lossy(units_view units) noexcept {
        FixedString text;
        static_cast<void>(text.assign_lossy(units));
        return text;
    }

    // Valid text, copied whole: nothing when it holds more than MaxCodePoints code points.
    [[nodiscard]] static constexpr std::optional<FixedString> from(StringView<E> text) noexcept {
        if (text.count_code_points() > MaxCodePoints) return std::nullopt;
        FixedString made;
        std::ranges::copy(text.cells(), made.cells_.begin());
        made.count_ = text.count_code_points();
        return made;
    }

    constexpr void clear() noexcept { count_ = 0; }

    [[nodiscard]] constexpr StringView<E> view() const noexcept { return StringView<E>{std::span{cells_}.first(count_ * cell_units)}; }
    constexpr operator StringView<E>() const noexcept { return view(); }

    [[nodiscard]] constexpr bool empty() const noexcept { return count_ == 0; }
    [[nodiscard]] constexpr std::size_t count_code_points() const noexcept { return count_; }

    [[nodiscard]] friend constexpr bool operator==(const FixedString& a, StringView<E> b) noexcept { return a.view() == b; }
    [[nodiscard]] friend constexpr std::strong_ordering operator<=>(const FixedString& a, StringView<E> b) noexcept { return a.view() <=> b; }

    friend std::ostream& operator<<(std::ostream& out, const FixedString& text)
        requires std::same_as<unit, char>
    {
        return out << text.view();
    }

private:
    std::array<unit, MaxCodePoints * E::cell_units> cells_{};
    std::size_t count_{};
};

namespace detail {

template <Encoding E, std::size_t N>
[[nodiscard]] constexpr String<E, N> joined(StringView<E> a, StringView<E> b) {
    String<E, N> made;
    made.reserve_code_points(a.count_code_points() + b.count_code_points());
    made += a;
    made += b;
    return made;
}

}  // namespace detail

// Joining makes a new String, in one block. Each form takes text of one encoding only, so two encodings cannot be
// joined by mistake.
template <Encoding E, std::size_t N>
[[nodiscard]] constexpr String<E, N> operator+(const String<E, N>& a, const String<E, N>& b) {
    return detail::joined<E, N>(a, b);
}
template <Encoding E, std::size_t N>
[[nodiscard]] constexpr String<E, N> operator+(const String<E, N>& a, std::type_identity_t<StringView<E>> b) {
    return detail::joined<E, N>(a, b);
}
template <Encoding E, std::size_t N>
[[nodiscard]] constexpr String<E, N> operator+(std::type_identity_t<StringView<E>> a, const String<E, N>& b) {
    return detail::joined<E, N>(a, b);
}
template <Encoding E>
[[nodiscard]] constexpr String<E> operator+(StringView<E> a, std::type_identity_t<StringView<E>> b) {
    return detail::joined<E, default_inline_cells>(a, b);
}

}  // namespace jstr::inline v001

template <jstr::Encoding E, std::size_t N>
struct std::hash<jstr::String<E, N>> {
    [[nodiscard]] std::size_t operator()(const jstr::String<E, N>& text) const noexcept { return std::hash<jstr::StringView<E>>{}(text.view()); }
};

// std::format("{}", text), with every option a std::string_view takes, given the text packed.
template <jstr::Encoding E>
    requires std::same_as<typename E::unit, char>
struct std::formatter<jstr::StringView<E>, char> : std::formatter<std::string_view, char> {
    template <class Context>
    auto format(jstr::StringView<E> text, Context& context) const {
        const std::vector<char> packed = text.to_packed();
        return std::formatter<std::string_view, char>::format(std::string_view{packed.begin(), packed.end()}, context);
    }
};

template <jstr::Encoding E, std::size_t N>
    requires std::same_as<typename E::unit, char>
struct std::formatter<jstr::String<E, N>, char> : std::formatter<jstr::StringView<E>, char> {
    template <class Context>
    auto format(const jstr::String<E, N>& text, Context& context) const {
        return std::formatter<jstr::StringView<E>, char>::format(text.view(), context);
    }
};

template <std::size_t N, jstr::Encoding E>
    requires std::same_as<typename E::unit, char>
struct std::formatter<jstr::FixedString<N, E>, char> : std::formatter<jstr::StringView<E>, char> {
    template <class Context>
    auto format(const jstr::FixedString<N, E>& text, Context& context) const {
        return std::formatter<jstr::StringView<E>, char>::format(text.view(), context);
    }
};
