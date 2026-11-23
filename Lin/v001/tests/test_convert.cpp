// File: tests/test_convert.cpp
// Purpose: convert<To>() between every pair of Types, all 25: the text comes out the same code points, short and long
//          (inside the String and on the heap), and back again. To Ascii, text above U+007F is refused with the first
//          code point that does not fit, and convert_lossy<Type::Ascii>() makes each one '?'.
#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <type_traits>

#include "check.hpp"
#include "jString/v001/jstring.hpp"
#include "text_helpers.hpp"

namespace {

using jString::ConversionError;
using jString::String;
using jString::Type;
using jstr_test::packed;
using jstr_test::runtime_units;
using namespace jString::literals;

constexpr std::string_view ascii_text = "plain text, and long enough to go to the heap in every Type: 0123456789 0123456789";
constexpr std::string_view mixed_text = "aé日\U0001F600 the café naïve über fox \U0001F600 end, and long enough for the heap.";

template <Type From, Type To>
void one_pair(jstr_test::Checker& check) {
    // ASCII text: every pair, and To Ascii it is never refused.
    for (const std::string_view utf8 : {std::string_view{}, std::string_view{"abc"}, ascii_text}) {
        const String<From> source = String<From>::from(runtime_units<From>(utf8)).value();
        const auto converted = jString::convert<To>(source);
        if constexpr (To == Type::Ascii && From != Type::Ascii) {
            check(converted.has_value() && packed<To>(*converted) == utf8, "ASCII text to Ascii");
        } else {
            static_assert(std::is_same_v<std::remove_cvref_t<decltype(converted)>, String<To>>, "never fails, so no std::expected");
            check(packed<To>(converted) == utf8 && converted.count_code_points() == utf8.size(), "ASCII text, the same code points");
            check(jString::convert<From>(converted) == source, "and back");
        }
    }
    // Text above U+007F: every pair but those from Ascii, which cannot hold it.
    if constexpr (From != Type::Ascii) {
        for (const std::string_view utf8 : {std::string_view{"é\U0001F600"}, mixed_text}) {
            const String<From> source = String<From>::from(runtime_units<From>(utf8)).value();
            const auto converted = jString::convert<To>(source.view());
            if constexpr (To == Type::Ascii) {
                const std::size_t first_wide = utf8 == mixed_text ? 1 : 0;
                check(!converted.has_value() && converted.error() == ConversionError{first_wide, U'é'}, "to Ascii: the first code point that does not fit");
                const auto lossy = jString::convert_lossy<Type::Ascii>(source);
                check(lossy.count_code_points() == source.count_code_points() && (utf8 != mixed_text || lossy.substring(jString::CodePointIndex{0}, jString::CodePointCount{5}) == "a??? "_js),
                      "convert_lossy: a '?' for each");
            } else {
                check(packed<To>(converted) == utf8 && converted.count_code_points() == source.count_code_points(), "the same code points");
                check(jString::convert<From>(converted) == source, "and back");
                check(converted.at(jString::CodePointIndex{1}) == *source.view().at(jString::CodePointIndex{1})->code_points().begin(), "code point 1 is code point 1");
            }
        }
    }
    // A FixedString converts as its text.
    const jString::FixedString<From, 4> fixed = jString::FixedString<From, 4>::from("ab"_js).value();
    if constexpr (To == Type::Ascii && From != Type::Ascii) {
        check(jString::convert<To>(fixed).value() == "ab"_js, "a FixedString");
    } else {
        check(jString::convert<To>(fixed) == "ab"_js, "a FixedString");
    }
}

template <Type From>
void from_each(jstr_test::Checker& check) {
    check.context(jstr_test::name_of(From));
    one_pair<From, Type::Ascii>(check);
    one_pair<From, Type::UTF8Packed>(check);
    one_pair<From, Type::UTF8Full>(check);
    one_pair<From, Type::UTF16>(check);
    one_pair<From, Type::UTF32>(check);
}

}  // namespace

int main() {
    jstr_test::Checker check;
    from_each<Type::Ascii>(check);
    from_each<Type::UTF8Packed>(check);
    from_each<Type::UTF8Full>(check);
    from_each<Type::UTF16>(check);
    from_each<Type::UTF32>(check);

    // The inside of a converted String: one chosen by name.
    check.context("");
    const String<Type::UTF16> wide = "short"_js;
    const auto tiny = jString::convert<Type::UTF8Full, 0>(wide);
    static_assert(std::is_same_v<std::remove_cvref_t<decltype(tiny)>, String<Type::UTF8Full, 0>>);
    check(tiny == "short"_js, "into a String with no room inside");
    return check.finish();
}
