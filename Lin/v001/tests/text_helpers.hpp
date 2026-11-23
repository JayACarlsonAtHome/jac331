// File: tests/text_helpers.hpp
// Purpose: What the tests of every Type share: its name for a failure, units made at run time from UTF-8 (so the
//          compiler cannot check them), and text read back as UTF-8.
#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "jString/v001/jstring.hpp"

namespace jstr_test {

using jString::StringView;
using jString::Type;

template <Type T>
using unit_t = typename StringView<T>::unit;
template <Type T>
using units_t = std::basic_string<unit_t<T>>;

[[nodiscard]] constexpr const char* name_of(Type type) {
    switch (type) {
        case Type::Ascii: return "Ascii: ";
        case Type::UTF8Packed: return "UTF8Packed: ";
        case Type::UTF8Full: return "UTF8Full: ";
        case Type::UTF16: return "UTF16: ";
        case Type::UTF32: return "UTF32: ";
        default: return "?: ";
    }
}

// Text built at run time, so the compiler cannot check it: the way units from a file or a C library arrive. Valid
// UTF-8 in, written as T's units: the same bytes for the char Types, encoded for UTF16 and UTF32.
template <Type T>
[[nodiscard]] units_t<T> runtime_units(std::string_view utf8) {
    if constexpr (std::is_same_v<unit_t<T>, char>) {
        return std::string{utf8};
    } else {
        using codec = std::conditional_t<T == Type::UTF16, jString::Utf16, jString::Utf32>;
        units_t<T> out;
        std::size_t at = 0;
        while (at < utf8.size()) {
            const jString::Scan scan = jString::Utf8::scan(utf8.substr(at));
            std::array<unit_t<T>, codec::max_units> one{};
            const std::size_t length = codec::encode(scan.code_point, one);
            out.append(one.data(), length);
            at += scan.length;
        }
        return out;
    }
}

// Text as UTF-8, as a std::string, to compare with the bytes it was made from.
template <Type T>
[[nodiscard]] std::string packed(StringView<T> text) {
    const std::vector<char> units = text.to_utf8();
    return std::string{units.begin(), units.end()};
}

}  // namespace jstr_test
