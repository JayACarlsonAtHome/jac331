// File: tests/test_constexpr.cpp
// Purpose: The same promises as test_utf8.cpp and test_string.cpp, proven while compiling. A constant evaluation
//          stops at undefined behaviour, a leak, or a read out of bounds, so each static_assert here also proves the
//          code it runs has none. Running it only reports that it was built.
#include <iostream>
#include <utility>
#include <vector>

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

static_assert(StringView<>{"café"_js}.count_code_points() == 4);
static_assert(StringView<>{"café"_js}.size_utf8() == 5);
static_assert(String<>::from("\x80").error() == EncodingError{0});
static_assert(StringView<>{"a=b"_js}.split_once("="_js)->second == "b"_js);
static_assert(StringView<>{"a"_js} < StringView<>{"é"_js});

static_assert(StringView<>{"Th\u00e9 is a test"_js}.at(jstr::CodePointIndex{5}) == U's');
static_assert(!StringView<>{"abc"_js}.at(jstr::CodePointIndex{3}).has_value());

static_assert(StringView<>{"\u00e9t\u00e9"_js}.at(jstr::CodePointNumber{3}) == U'\u00e9');
static_assert(StringView<>{"abcdef"_js}.substring(jstr::CodePointNumber{2}, jstr::CodePointCount{3}) == "bcd"_js);
static_assert(!StringView<>{"abc"_js}.substring(jstr::CodePointIndex{2}, jstr::CodePointCount{2}).has_value());
static_assert(StringView<>{"abc"_js}.replace(jstr::CodePointNumber{2}, jstr::CodePointCount{1}, "\u00e9"_js) == "a\u00e9c"_js);
static_assert(jstr::CodePointNumber{7}.skipped() == 6U && jstr::CodePointIndex{6}.skipped() == 6U && !jstr::CodePointNumber{0}.skipped());

// A literal's cells, made while compiling: its bytes packed back are the literal's.
static_assert([] {
    const std::vector<char> bytes = "a\u00e9\U0001F600"_js.view().to_utf8();
    return bytes == std::vector<char>{'a', '\xC3', '\xA9', '\xF0', '\x9F', '\x98', '\x80'};
}());

// A String allocates and frees inside the evaluation: each one must free all it took.
static_assert([] {
    String<> text = "ab"_js;
    text += text;
    text += "é"_js;
    return text == "ababé"_js && text.count_code_points() == 5;
}());
static_assert([] {
    String<> from = "x"_js;
    const String<> to{std::move(from)};
    return from.empty() && to == "x"_js;
}());
// Text moving from inside a String to the heap, made and used while compiling.
static_assert([] {
    jstr::String<Utf8, 4> text = "a\u00e9"_js;
    text += "b\u65e5c"_js;
    text += text;
    return text == "a\u00e9b\u65e5ca\u00e9b\u65e5c"_js && text.count_code_points() == 10 && text.at(jstr::CodePointIndex{8}) == U'\u65e5' &&
           text.substring(jstr::CodePointNumber{2}, jstr::CodePointCount{3}) == StringView<>{"\u00e9b\u65e5"_js};
}());
static_assert([] {
    jstr::String<Utf8, 4> from = "\u00e9\u00e9\u00e9"_js;
    const jstr::String<Utf8, 4> to{std::move(from)};
    return from.empty() && from.count_code_points() == 0 && to.at(jstr::CodePointIndex{2}) == U'\u00e9';
}());
static_assert([] {
    // Short text copied onto text on the heap gives up the heap's room, and still grows.
    jstr::String<Utf8, 4> target = "long text, on the heap"_js;
    const jstr::String<Utf8, 4> small = "ab"_js;
    target = small;
    target += target;
    return target == "abab"_js;
}());
static_assert(StringView<>{"0123456789abcdef0123456789\u00e9"_js}.at(jstr::CodePointIndex{26}) == U'\u00e9');
static_assert(String<>::from("ok\xFF").error() == EncodingError{2});
static_assert(String<>::from_lossy("a\xF0\x80\x80" "b") == "a���" "b"_js);
static_assert(("x"_js.view() + "y"_js) + String<>{"z"_js} == "xyz"_js);
static_assert(!String<>::from(std::string_view{"a\0b", 3})->to_c_string().has_value());
static_assert(String<>::from(std::string_view{"\xC3\xA9\0x", 4})->split_once(*String<>::from(std::string_view{"\0", 1})->at(jstr::CodePointIndex{0}))->second == "x"_js);

// FixedString: cut, made U+FFFD, and read, all while compiling.
static_assert([] {
    jstr::FixedString<3> text;
    const std::size_t kept = text.assign_lossy("\xC3\xA9\xFFxyz");
    return kept == 3 && text == "\u00e9\uFFFDx"_js;
}());

}  // namespace

int main() {
    std::cout << "every check passed while compiling\n";
    return 0;
}
