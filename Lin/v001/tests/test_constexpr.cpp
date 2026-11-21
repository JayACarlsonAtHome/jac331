// File: tests/test_constexpr.cpp
// Purpose: The same promises as test_utf8.cpp and test_string.cpp, proven while compiling. A constant evaluation
//          stops at undefined behaviour, a leak, or a read out of bounds, so each static_assert here also proves the
//          code it runs has none. Running it only reports that it was built.
#include <iostream>
#include <utility>

#include "jString/v001/jstring.hpp"

namespace {

using jstr::EncodingError;
using jstr::Scan;
using jstr::String;
using jstr::StringView;
using jstr::Utf8;
using namespace jstr::literals;

static_assert(Utf8::scan("\xF4\x8F\xBF\xBF") == Scan{U'\U0010FFFF', 4, true});
static_assert(Utf8::scan("\xED\xA0\x80") == Scan{jstr::replacement_character, 1, false});
static_assert(Utf8::scan("\xE1\x80") == Scan{jstr::replacement_character, 2, false});
static_assert(jstr::first_invalid<Utf8>("ab\xC0") == EncodingError{2});

static_assert(StringView<>{"café"}.count_code_points() == 4);
static_assert(StringView<>{"café"}.size_units() == 5);
static_assert(StringView<>::from("\x80").error() == EncodingError{0});
static_assert(StringView<>{"a=b"}.split_once("=")->second == "b");
static_assert(StringView<>{"a"} < StringView<>{"é"});

static_assert(StringView<>{"Th\u00e9 is a test"}.at(jstr::CodePointIndex{5}) == U's');
static_assert(!StringView<>{"abc"}.at(jstr::CodePointIndex{3}).has_value());

static_assert(StringView<>{"\u00e9t\u00e9"}.at(jstr::CodePointNumber{3}) == U'\u00e9');
static_assert(StringView<>{"abcdef"}.substring(jstr::CodePointNumber{2}, jstr::CodePointCount{3}) == "bcd");
static_assert(!StringView<>{"abc"}.substring(jstr::CodePointIndex{2}, jstr::CodePointCount{2}).has_value());
static_assert(StringView<>{"abc"}.replace(jstr::CodePointNumber{2}, jstr::CodePointCount{1}, "\u00e9") == "a\u00e9c"_js);
static_assert(jstr::CodePointNumber{7}.skipped() == 6U && jstr::CodePointIndex{6}.skipped() == 6U && !jstr::CodePointNumber{0}.skipped());

// A String allocates and frees inside the evaluation: each one must free all it took.
static_assert([] {
    String<> text = "ab"_js;
    text += text;
    text += "é";
    return text == "ababé" && text.count_code_points() == 5;
}());
static_assert([] {
    String<> from = "x"_js;
    const String<> to{std::move(from)};
    return from.empty() && to == "x";
}());
static_assert(String<>::from("ok\xFF").error() == EncodingError{2});
static_assert(String<>::from_lossy("a\xF0\x80\x80" "b") == "a���" "b");
static_assert(("x"_js.view() + "y") + String<>{"z"_js} == "xyz");
static_assert(!String<>::from(std::string_view{"a\0b", 3})->c_str().has_value());

}  // namespace

int main() {
    std::cout << "every check passed while compiling\n";
    return 0;
}
