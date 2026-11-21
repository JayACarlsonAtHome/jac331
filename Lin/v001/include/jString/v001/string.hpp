// File: include/jString/v001/string.hpp
// Purpose: String<E>: text it owns, always valid in encoding E (UTF-8 unless named). Made only from text already
//          known to be valid (a "text"_js literal, or a StringView, by name), from units through from(), which says
//          where they first go wrong, or through from_lossy(), which puts U+FFFD where they do.
//
//          Short text (under InlineUnits units, 64 unless named: one cache line) is held inside the String, and longer text on the heap
//          in a std::vector; either way with one terminating zero after it. Where it is, is worked out each time, never
//          kept as a pointer: nothing in a String points into the String itself, so no compiler can take a moved or
//          destroyed String's pointer for one into the stack (g++ 15's -Wdangling-pointer did, with std::string's
//          small-string buffer).
//
//          Every String knows how many code points it holds. Text on the heap that is not all one unit to a code point
//          also keeps a table of where each code point starts, 4 bytes each, so at() and substring() answer at once.
//
//          Promises a caller can build on:
//            - a String moved from is empty
//            - a view into a String is good until the String is changed, moved from or destroyed: short text is inside
//              the String, and moves with it
//            - s.append(s) and s += s double s
//            - c_str() gives nothing, rather than a shortened string, when the text holds a zero
#pragma once

#include <algorithm>
#include <array>
#include <compare>
#include <concepts>
#include <cstddef>
#include <expected>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <ostream>
#include <ranges>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "encoding.hpp"
#include "string_view.hpp"

namespace jstr::inline v001 {

template <Encoding E, std::size_t InlineUnits>
class String {
    static_assert(InlineUnits >= 1, "a String holds at least its terminating zero inside itself");

public:
    using encoding = E;
    using unit = typename E::unit;
    using units_view = std::basic_string_view<unit>;
    static constexpr std::size_t inline_units = InlineUnits;

    constexpr String() noexcept = default;

    // A literal: checked while compiling, so this is the one way in that needs no name. String<> s = "text"_js;
    constexpr String(Literal<E> text) : String{text.view()} {}

    // Text someone else owns, copied. Named, so that every copy is seen: String<> s{view};
    constexpr explicit String(StringView<E> text) { place(text.units(), code_point_count<E>(text.units())); }

    // Units from anywhere else: valid text, or where it first goes wrong.
    [[nodiscard]] static constexpr std::expected<String, EncodingError> from(units_view units) {
        const auto counted = validate<E>(units);
        if (!counted) return std::unexpected(counted.error());
        String text;
        text.place(units, *counted);
        return text;
    }

    // Units from anywhere else, each bad sequence made U+FFFD: for text that must be shown whatever it holds, such as
    // a file name.
    [[nodiscard]] static constexpr String from_lossy(units_view units) {
        String text;
        if (const auto counted = validate<E>(units)) {
            text.place(units, *counted);
            return text;
        }
        std::vector<unit> made;
        made.reserve(units.size() + E::replacement().size());
        std::size_t count = 0;
        std::size_t at = 0;
        while (at < units.size()) {
            const Scan scan = E::scan(units.substr(at));
            const units_view piece = scan.valid ? units.substr(at, scan.length) : E::replacement();
            made.insert(made.end(), piece.begin(), piece.end());
            at += scan.length;
            ++count;
        }
        text.place(units_view{std::span{made}}, count);
        return text;
    }

    constexpr String(const String&) = default;
    constexpr String& operator=(const String&) = default;
    // A vector moved by its constructor is left empty, as the standard says. (std::exchange(other.heap_, {}) says the
    // same, and set off g++ 16's -Warray-bounds on a String moved into a std::unordered_set's node.)
    constexpr String(String&& other) noexcept
        : inside_{other.inside_},
          heap_{std::move(other.heap_)},
          starts_{std::move(other.starts_)},
          size_{std::exchange(other.size_, 0)},
          count_{std::exchange(other.count_, 0)} {
        other.inside_.front() = unit{};
    }
    // A vector moved by assignment is left valid, but the standard does not say empty, so it is made so.
    constexpr String& operator=(String&& other) noexcept {
        if (this != &other) {
            inside_ = other.inside_;
            heap_ = std::move(other.heap_);
            starts_ = std::move(other.starts_);
            size_ = std::exchange(other.size_, 0);
            count_ = std::exchange(other.count_, 0);
            other.heap_.clear();
            other.starts_.clear();
            other.inside_.front() = unit{};
        }
        return *this;
    }
    constexpr ~String() = default;

    [[nodiscard]] constexpr StringView<E> view() const noexcept { return StringView<E>{text_units()}; }
    constexpr operator StringView<E>() const noexcept { return view(); }

    [[nodiscard]] constexpr units_view units() const noexcept { return text_units(); }
    [[nodiscard]] constexpr std::size_t size_units() const noexcept { return size_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] constexpr CodePoints<E> code_points() const noexcept { return view().code_points(); }
    // Counted when the text was made or added to, so this costs nothing.
    [[nodiscard]] constexpr std::size_t count_code_points() const noexcept { return count_; }

    // These give views into this String, so a String about to be destroyed has none to give: calling them on a
    // temporary does not compile. Each answers at once: by unit when every code point is one unit (in UTF-8, ASCII),
    // from the table when the text is on the heap, and by counting 16 units at a step when it is short enough to be
    // inside, which is at most InlineUnits units to count.
    template <IndexBase Base>
    [[nodiscard]] constexpr std::optional<StringView<E>> at(CodePointPosition<Base> position) const& noexcept {
        return substring(position, CodePointCount{1});
    }
    template <IndexBase Base>
    std::optional<StringView<E>> at(CodePointPosition<Base>) const&& = delete;
    template <IndexBase Base>
    [[nodiscard]] constexpr std::optional<StringView<E>> substring(CodePointPosition<Base> start, CodePointCount count) const& noexcept {
        const auto span = unit_span(start, count.value);
        if (!span) return std::nullopt;
        return StringView<E>{text_units().substr(span->first, span->second - span->first)};
    }
    template <IndexBase Base>
    std::optional<StringView<E>> substring(CodePointPosition<Base>, CodePointCount) const&& = delete;
    template <IndexBase Base>
    [[nodiscard]] constexpr std::optional<StringView<E>> substring(CodePointPosition<Base> start) const& noexcept {
        const auto skipped = start.skipped();
        if (!skipped || *skipped > count_) return std::nullopt;
        return substring(start, CodePointCount{count_ - *skipped});
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
        const auto span = unit_span(start, count.value);
        if (!span) return std::nullopt;
        const units_view text = text_units();
        String made;
        made.reserve_units(span->first + with.size_units() + (text.size() - span->second));
        made += StringView<E>{text.substr(0, span->first)};
        made += with;
        made += StringView<E>{text.substr(span->second)};
        return made;
    }

    [[nodiscard]] constexpr bool starts_with(StringView<E> prefix) const noexcept { return view().starts_with(prefix); }
    [[nodiscard]] constexpr bool ends_with(StringView<E> suffix) const noexcept { return view().ends_with(suffix); }
    [[nodiscard]] constexpr bool contains(StringView<E> part) const noexcept { return view().contains(part); }

    // For a C library: the units and a terminating zero. Nothing when the text holds a zero, because C would stop there
    // and quietly lose the rest.
    // Searched with std::ranges::find, which compares places: string_view's find tests a pointer for null, which g++ 15
    // cannot do while compiling when the pointer is into a String held inside a std::expected.
    [[nodiscard]] constexpr std::optional<const unit*> c_str() const noexcept {
        const units_view text = text_units();
        if (std::ranges::find(text, unit{}) != text.end()) return std::nullopt;
        return heap_.empty() ? inside_.data() : heap_.data();
    }

    // Room for this many units of text without making room again: for a String about to be built up in pieces.
    constexpr void reserve_units(std::size_t units) {
        if (units < room()) return;
        grow(units + 1);
    }

    // Adds text at the end. The text may be this String's own: it is read in full before anything it is in is let go.
    // When there is no room, the room is doubled, so a String built by many appends is copied about once, not once per
    // append.
    constexpr String& append(StringView<E> more) {
        if (more.empty()) return *this;
        const std::size_t added = code_point_count<E>(more.units());
        const std::size_t before = size_;
        const std::size_t total = before + more.size_units();
        if (total >= room()) {
            std::vector<unit> made;
            made.reserve(std::max(total + 1, 2 * room()));
            const units_view mine = text_units();
            made.insert(made.end(), mine.begin(), mine.end());
            made.insert(made.end(), more.units().begin(), more.units().end());
            made.push_back(unit{});
            heap_ = std::move(made);
        } else if (heap_.empty()) {
            // Nothing moves. more, if it is this String's own text, lies before where the new text goes, so nothing it
            // holds is written over.
            std::ranges::copy(more.units(), std::span{inside_}.subspan(before).begin());
            inside_[total] = unit{};
        } else {
            heap_.resize(total + 1);
            std::ranges::copy(more.units(), std::span{heap_}.subspan(before).begin());
            heap_[total] = unit{};
        }
        size_ = total;
        count_ += added;
        index_after_append(before, count_ - added);
        return *this;
    }
    constexpr String& operator+=(StringView<E> more) { return append(more); }

    // String with String is its own pair, or a == b would match the String-with-view pair both ways round, and C++20
    // calls that ambiguous. A template, so that it takes a String only, and a literal still goes to the pair below.
    template <std::same_as<String> Other>
    [[nodiscard]] friend constexpr bool operator==(const String& a, const Other& b) noexcept { return a.view() == b.view(); }
    template <std::same_as<String> Other>
    [[nodiscard]] friend constexpr auto operator<=>(const String& a, const Other& b) noexcept { return a.view() <=> b.view(); }
    [[nodiscard]] friend constexpr bool operator==(const String& a, StringView<E> b) noexcept { return a.view() == b; }
    [[nodiscard]] friend constexpr auto operator<=>(const String& a, StringView<E> b) noexcept { return a.view() <=> b; }
    template <std::same_as<char32_t> C>
    [[nodiscard]] friend constexpr bool operator==(const String& text, C code_point) noexcept { return text.view() == code_point; }

    friend std::ostream& operator<<(std::ostream& out, const String& text)
        requires std::same_as<unit, char>
    {
        return out << text.view();
    }

private:
    // The table holds 4-byte offsets, so text longer than that is counted instead.
    static constexpr std::size_t most_indexed = std::numeric_limits<std::uint32_t>::max();

    // How many units fit, the terminating zero among them, where the text is now.
    [[nodiscard]] constexpr std::size_t room() const noexcept { return heap_.empty() ? InlineUnits : heap_.capacity(); }

    [[nodiscard]] constexpr units_view text_units() const noexcept {
        if (heap_.empty()) return units_view{std::span{inside_}.first(size_)};
        return units_view{std::span{heap_}.first(size_)};
    }

    // In UTF-8: ASCII. The n-th code point is the n-th unit, and no table is needed.
    [[nodiscard]] constexpr bool one_unit_each() const noexcept { return count_ == size_; }

    // Moves the text to the heap, with room for units units, the terminating zero among them.
    constexpr void grow(std::size_t units) {
        const bool was_inside = heap_.empty();
        std::vector<unit> made;
        made.reserve(units);
        const units_view mine = text_units();
        made.insert(made.end(), mine.begin(), mine.end());
        made.push_back(unit{});
        heap_ = std::move(made);
        if (was_inside) index();  // on the heap already, every offset in the table is still right
    }

    // Only on a String just made: valid units, and how many code points they hold.
    constexpr void place(units_view units, std::size_t count) {
        size_ = units.size();
        count_ = count;
        if (units.size() < InlineUnits) {
            std::ranges::copy(units, inside_.begin());
            inside_[units.size()] = unit{};
            return;
        }
        heap_.reserve(units.size() + 1);
        heap_.assign(units.begin(), units.end());
        heap_.push_back(unit{});
        index();
    }

    // The table: where each code point starts, then the size. Kept only for text on the heap that is not one unit to
    // each code point and fits 4-byte offsets; empty otherwise.
    [[nodiscard]] constexpr bool wants_table() const noexcept { return !heap_.empty() && !one_unit_each() && size_ <= most_indexed; }

    constexpr void index() {
        if (!wants_table()) {
            starts_ = std::vector<std::uint32_t>{};
            return;
        }
        index_from(0, 0);
    }

    constexpr void index_after_append(std::size_t before, std::size_t old_count) {
        if (!wants_table()) {
            starts_ = std::vector<std::uint32_t>{};
        } else if (starts_.empty()) {
            index_from(0, 0);
        } else {
            index_from(before, old_count);
        }
    }

    // Fills the table from the code point numbered code_point, which starts at unit at. 16 units that are each a whole
    // code point (in UTF-8, ASCII) are 16 entries in a row, written in one step. Otherwise every unit's offset is
    // written to the slot of the code point it is in, and only a unit that starts one moves on to the next slot: no
    // branch, and no wait on one unit to know where the next code point is, so the loop stays fast on mixed text.
    // The text is valid, so there are exactly count_ code points, then the size.
    constexpr void index_from(std::size_t at, std::size_t code_point) {
        starts_.resize(count_ + 1);
        const units_view text = text_units();
        auto slot = starts_.begin() + static_cast<std::ptrdiff_t>(code_point);
        while (text.size() - at >= detail::block_units) {
            const units_view block = text.substr(at, detail::block_units);
            if (detail::all_whole_alone<E>(block)) {
                for (std::size_t i = 0; i < detail::block_units; ++i) slot[static_cast<std::ptrdiff_t>(i)] = static_cast<std::uint32_t>(at + i);
                slot += static_cast<std::ptrdiff_t>(detail::block_units);
            } else {
                std::size_t here = at;
                for (const unit u : block) {
                    *slot = static_cast<std::uint32_t>(here);
                    slot += E::starts_code_point(u) ? 1 : 0;
                    ++here;
                }
            }
            at += detail::block_units;
        }
        for (const unit u : text.substr(at)) {
            *slot = static_cast<std::uint32_t>(at);
            slot += E::starts_code_point(u) ? 1 : 0;
            ++at;
        }
        *slot = static_cast<std::uint32_t>(size_);
    }

    // Where, in units, the count code points from start begin and end. Nothing when start, or any of the count, is
    // past the end.
    template <IndexBase Base>
    [[nodiscard]] constexpr std::optional<std::pair<std::size_t, std::size_t>> unit_span(CodePointPosition<Base> start, std::size_t count) const noexcept {
        const auto skipped = start.skipped();
        if (!skipped || *skipped > count_ || count > count_ - *skipped) return std::nullopt;
        const std::size_t first = *skipped;
        const std::size_t last = first + count;
        if (one_unit_each()) return std::pair{first, last};
        if (!starts_.empty()) {
            const std::span<const std::uint32_t> table{starts_};
            return std::pair{std::size_t{table[first]}, std::size_t{table[last]}};
        }
        return view().unit_span(start, count);
    }

    // Text shorter than InlineUnits lives here, then a zero, and heap_ is empty. Longer text lives in heap_, then a
    // zero. Where the text is, is worked out from heap_ each time it is needed, and never kept as a pointer, so a String
    // never points into itself: moving one copies inside_ and moves the vectors' pointers, and no compiler can take a
    // pointer in a moved or destroyed String for one into the stack (g++ 15's -Wdangling-pointer did, with std::string).
    std::array<unit, InlineUnits> inside_{};
    std::vector<unit> heap_{};
    std::vector<std::uint32_t> starts_{};
    std::size_t size_{};
    std::size_t count_{};
};

namespace detail {

template <Encoding E, std::size_t N>
[[nodiscard]] constexpr String<E, N> joined(StringView<E> a, StringView<E> b) {
    String<E, N> made;
    made.reserve_units(a.size_units() + b.size_units());
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
    return detail::joined<E, default_inline_units>(a, b);
}

}  // namespace jstr::inline v001

template <jstr::Encoding E, std::size_t N>
struct std::hash<jstr::String<E, N>> {
    [[nodiscard]] std::size_t operator()(const jstr::String<E, N>& text) const noexcept { return std::hash<jstr::StringView<E>>{}(text.view()); }
};

// std::format("{}", text), with every option a std::string_view takes.
template <jstr::Encoding E>
    requires std::same_as<typename E::unit, char>
struct std::formatter<jstr::StringView<E>, char> : std::formatter<std::string_view, char> {
    template <class Context>
    auto format(jstr::StringView<E> text, Context& context) const {
        return std::formatter<std::string_view, char>::format(text.units(), context);
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
