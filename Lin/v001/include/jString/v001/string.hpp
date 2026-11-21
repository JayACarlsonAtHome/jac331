// File: include/jString/v001/string.hpp
// Purpose: String<E>: text it owns, always valid in encoding E (UTF-8 unless named). Made only from text already
//          known to be valid (a "text"_js literal, or a StringView, by name), from units through from(), which says
//          where they first go wrong, or through from_lossy(), which puts U+FFFD where they do.
//
//          Its units are always on the heap, in a std::vector, with one terminating zero after them when there are
//          any. Nothing in a String ever points into the String itself, so moving one moves three pointers, and no
//          compiler can take a moved or destroyed String's pointer for one into the stack (g++ 15's
//          -Wdangling-pointer did, with std::string's small-string buffer).
//
//          Promises a caller can build on:
//            - a String moved from is empty
//            - s.append(s) and s += s double s
//            - c_str() gives nothing, rather than a shortened string, when the text holds a zero
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

#include "encoding.hpp"
#include "string_view.hpp"

namespace jstr::inline v001 {

template <Encoding E = Utf8>
class String {
public:
    using encoding = E;
    using unit = typename E::unit;
    using units_view = std::basic_string_view<unit>;

    constexpr String() noexcept = default;

    // A literal: checked while compiling, so this is the one way in that needs no name. String<> s = "text"_js;
    constexpr String(Literal<E> text) : String{text.view()} {}

    // Text someone else owns, copied. Named, so that every copy is seen: String<> s{view};
    constexpr explicit String(StringView<E> text) { assign(text.units()); }

    // Units from anywhere else: valid text, or where it first goes wrong.
    [[nodiscard]] static constexpr std::expected<String, EncodingError> from(units_view units) {
        if (const auto bad = first_invalid<E>(units)) return std::unexpected(*bad);
        String text;
        text.assign(units);
        return text;
    }

    // Units from anywhere else, each bad sequence made U+FFFD: for text that must be shown whatever it holds, such as
    // a file name.
    [[nodiscard]] static constexpr String from_lossy(units_view units) {
        String text;
        std::vector<unit> made;
        made.reserve(units.size() + 1);
        std::size_t at = 0;
        while (at < units.size()) {
            const Scan scan = E::scan(units.substr(at));
            const units_view piece = scan.valid ? units.substr(at, scan.length) : E::replacement();
            made.insert(made.end(), piece.begin(), piece.end());
            at += scan.length;
        }
        if (!made.empty()) made.push_back(unit{});
        text.units_ = std::move(made);
        return text;
    }

    constexpr String(const String&) = default;
    constexpr String& operator=(const String&) = default;
    constexpr String(String&& other) noexcept : units_{std::exchange(other.units_, {})} {}
    constexpr String& operator=(String&& other) noexcept {
        if (this != &other) units_ = std::exchange(other.units_, {});
        return *this;
    }
    constexpr ~String() = default;

    [[nodiscard]] constexpr StringView<E> view() const noexcept { return StringView<E>{text_units()}; }
    constexpr operator StringView<E>() const noexcept { return view(); }

    [[nodiscard]] constexpr units_view units() const noexcept { return text_units(); }
    [[nodiscard]] constexpr std::size_t size_units() const noexcept { return text_units().size(); }
    [[nodiscard]] constexpr bool empty() const noexcept { return units_.empty(); }
    [[nodiscard]] constexpr CodePoints<E> code_points() const noexcept { return view().code_points(); }
    [[nodiscard]] constexpr std::size_t count_code_points() const noexcept { return view().count_code_points(); }

    // These give views into this String, so a String about to be destroyed has none to give: calling them on a
    // temporary does not compile.
    template <IndexBase Base>
    [[nodiscard]] constexpr std::optional<StringView<E>> at(CodePointPosition<Base> position) const& noexcept { return view().at(position); }
    template <IndexBase Base>
    std::optional<StringView<E>> at(CodePointPosition<Base>) const&& = delete;
    template <IndexBase Base>
    [[nodiscard]] constexpr std::optional<StringView<E>> substring(CodePointPosition<Base> start, CodePointCount count) const& noexcept {
        return view().substring(start, count);
    }
    template <IndexBase Base>
    std::optional<StringView<E>> substring(CodePointPosition<Base>, CodePointCount) const&& = delete;
    template <IndexBase Base>
    [[nodiscard]] constexpr std::optional<StringView<E>> substring(CodePointPosition<Base> start) const& noexcept { return view().substring(start); }
    template <IndexBase Base>
    std::optional<StringView<E>> substring(CodePointPosition<Base>) const&& = delete;
    [[nodiscard]] constexpr std::optional<std::pair<StringView<E>, StringView<E>>> split_once(StringView<E> separator) const& noexcept {
        return view().split_once(separator);
    }
    std::optional<std::pair<StringView<E>, StringView<E>>> split_once(StringView<E>) const&& = delete;

    // A new String; this one is not changed.
    template <IndexBase Base>
    [[nodiscard]] constexpr std::optional<String> replace(CodePointPosition<Base> start, CodePointCount count, StringView<E> with) const {
        return view().replace(start, count, with);
    }

    [[nodiscard]] constexpr bool starts_with(StringView<E> prefix) const noexcept { return view().starts_with(prefix); }
    [[nodiscard]] constexpr bool ends_with(StringView<E> suffix) const noexcept { return view().ends_with(suffix); }
    [[nodiscard]] constexpr bool contains(StringView<E> part) const noexcept { return view().contains(part); }

    // For a C library: the units and a terminating zero. Nothing when the text holds a zero, because C would stop there
    // and quietly lose the rest.
    [[nodiscard]] constexpr std::optional<const unit*> c_str() const noexcept {
        if (text_units().find(unit{}) != units_view::npos) return std::nullopt;
        return units_.empty() ? no_units.data() : units_.data();
    }

    // Adds text at the end. The text may be this String's own: it is read in full before anything is changed.
    constexpr String& append(StringView<E> more) {
        if (more.empty()) return *this;
        std::vector<unit> made;
        made.reserve(size_units() + more.size_units() + 1);
        const units_view mine = text_units();
        made.insert(made.end(), mine.begin(), mine.end());
        made.insert(made.end(), more.units().begin(), more.units().end());
        made.push_back(unit{});
        units_ = std::move(made);
        return *this;
    }
    constexpr String& operator+=(StringView<E> more) { return append(more); }

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
    static constexpr std::array<unit, 1> no_units{};

    [[nodiscard]] constexpr units_view text_units() const noexcept {
        if (units_.empty()) return {};
        return units_view{std::span{units_}.first(units_.size() - 1)};
    }

    constexpr void assign(units_view units) {
        std::vector<unit> made;
        if (!units.empty()) {
            made.reserve(units.size() + 1);
            made.assign(units.begin(), units.end());
            made.push_back(unit{});
        }
        units_ = std::move(made);
    }

    // The units, then one terminating zero; or nothing at all for empty text.
    std::vector<unit> units_{};
};

// Joining makes a new String. Each form takes text of one encoding only, so two encodings cannot be joined by mistake.
template <Encoding E>
[[nodiscard]] constexpr String<E> operator+(const String<E>& a, const String<E>& b) {
    String<E> joined{a};
    joined += b;
    return joined;
}
template <Encoding E>
[[nodiscard]] constexpr String<E> operator+(const String<E>& a, std::type_identity_t<StringView<E>> b) {
    String<E> joined{a};
    joined += b;
    return joined;
}
template <Encoding E>
[[nodiscard]] constexpr String<E> operator+(std::type_identity_t<StringView<E>> a, const String<E>& b) {
    String<E> joined{a};
    joined += b;
    return joined;
}
template <Encoding E>
[[nodiscard]] constexpr String<E> operator+(StringView<E> a, std::type_identity_t<StringView<E>> b) {
    String<E> joined{a};
    joined += b;
    return joined;
}

}  // namespace jstr::inline v001

template <jstr::Encoding E>
struct std::hash<jstr::String<E>> {
    [[nodiscard]] std::size_t operator()(const jstr::String<E>& text) const noexcept { return std::hash<jstr::StringView<E>>{}(text.view()); }
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

template <jstr::Encoding E>
    requires std::same_as<typename E::unit, char>
struct std::formatter<jstr::String<E>, char> : std::formatter<jstr::StringView<E>, char> {
    template <class Context>
    auto format(const jstr::String<E>& text, Context& context) const {
        return std::formatter<jstr::StringView<E>, char>::format(text.view(), context);
    }
};
