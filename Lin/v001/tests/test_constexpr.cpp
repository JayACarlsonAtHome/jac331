// File: tests/test_constexpr.cpp
// Purpose: The same promises as the run-time tests, proven while compiling, for every Type. A constant evaluation
//          stops at undefined behaviour, a leak, or a read out of bounds, so each static_assert here also proves the
//          code it runs has none. Running it only reports that it was built.
// Modified 2026-11-24 by Claude Opus 5.5: case changes proven while compiling, in every Type.
#include <iostream>
#include <string_view>
#include <utility>
#include <vector>

#include "jString/v001/jstring.hpp"

namespace {

using jString::CodePointCount;
using jString::CodePointIndex;
using jString::CodePointNumber;
using jString::EncodingError;
using jString::Scan;
using jString::String;
using jString::StringView;
using jString::Type;
using jString::Utf8;
using namespace jString::literals;

static_assert(Utf8::scan("\xF4\x8F\xBF\xBF") == Scan{U'\U0010FFFF', 4, true});
static_assert(Utf8::scan("\xED\xA0\x80") == Scan{jString::replacement_character, 1, false});
static_assert(Utf8::scan("\xE1\x80") == Scan{jString::replacement_character, 2, false});
static_assert(jString::first_invalid<Utf8>("ab\xC0") == EncodingError{2});
static_assert(jString::Utf16::scan(u"\U0001F600") == Scan{U'\U0001F600', 2, true});
static_assert(jString::CodePointNumber{7}.skipped() == 6U && jString::CodePointIndex{6}.skipped() == 6U && !jString::CodePointNumber{0}.skipped());

// What every Type does, while compiling: views of literals, Strings made, grown past their inside, moved, cut and
// replaced, every one freeing all it took.
template <Type T>
[[nodiscard]] consteval bool every_type() {
    using View = StringView<T>;
    bool good = View{"cafe"_js}.count_code_points() == 4 && View{"cafe"_js}.size_utf8() == 4;
    good = good && View{"a=b"_js}.split_once("="_js)->second == "b"_js && View{"a"_js} < View{"b"_js};
    good = good && View{"This is a test"_js}.at(CodePointIndex{5}) == U'i' && !View{"abc"_js}.at(CodePointIndex{3}).has_value();
    good = good && View{"abcdef"_js}.substring(CodePointNumber{2}, CodePointCount{3}) == "bcd"_js;
    good = good && View{"abc"_js}.replace(CodePointNumber{2}, CodePointCount{1}, "x"_js) == "axc"_js;
    String<T> text = "ab"_js;
    text += text;
    text += "c"_js;
    good = good && text == "ababc"_js && text.count_code_points() == 5;
    String<T, 4> small = "ab"_js;
    small += "cdefgh"_js;
    small += small;
    good = good && small == "abcdefghabcdefgh"_js && small.at(CodePointIndex{9}) == U'b';
    String<T, 4> from = "xyz"_js;
    const String<T, 4> to{std::move(from)};
    good = good && from.empty() && to == "xyz"_js;
    String<T, 4> target = "long text, on the heap"_js;
    const String<T, 4> two = "ab"_js;
    target = two;
    target += target;
    good = good && target == "abab"_js;
    good = good && ("x"_js.view<T>() + "y"_js) + String<T>{"z"_js} == "xyz"_js;
    jString::FixedString<T, 3> fixed;
    good = good && fixed.assign_lossy(typename jString::FixedString<T, 3>::lossy_view{}) == 0 && fixed.empty();
    return good;
}
static_assert(every_type<Type::Ascii>());
static_assert(every_type<Type::UTF8Packed>());
static_assert(every_type<Type::UTF8Full>());
static_assert(every_type<Type::UTF16>());
static_assert(every_type<Type::UTF32>());

// And with code points of every width, for the four Types that hold them.
template <Type T>
[[nodiscard]] consteval bool unicode_type() {
    using View = StringView<T>;
    bool good = View{"café"_js}.count_code_points() == 4 && View{"café"_js}.size_utf8() == 5 && View{"a"_js} < View{"é"_js};
    good = good && View{"Thé is a test"_js}.at(CodePointIndex{5}) == U's' && View{"été"_js}.at(CodePointNumber{3}) == U'é';
    good = good && View{"￿"_js} < View{"\U00010000"_js};
    good = good && View{"abc"_js}.replace(CodePointNumber{2}, CodePointCount{1}, "é"_js) == "aéc"_js;
    String<T, 4> text = "aé"_js;
    text += "b日\U0001F600"_js;
    text += text;
    good = good && text == "aéb日\U0001F600aéb日\U0001F600"_js && text.count_code_points() == 10 && text.at(CodePointIndex{8}) == U'日';
    good = good && text.substring(CodePointNumber{2}, CodePointCount{3}) == View{"éb日"_js};
    const std::vector<char> bytes = "aé\U0001F600"_js.view<T>().to_utf8();
    good = good && bytes == std::vector<char>{'a', '\xC3', '\xA9', '\xF0', '\x9F', '\x98', '\x80'};
    good = good && jString::convert<Type::UTF16>(text) == jString::convert<Type::UTF16>(jString::convert<Type::UTF32>(text));
    good = good && jString::convert<Type::Ascii>(View{"é"_js}).error() == jString::ConversionError{0, U'é'};
    return good;
}
static_assert(unicode_type<Type::UTF8Packed>());
static_assert(unicode_type<Type::UTF8Full>());
static_assert(unicode_type<Type::UTF16>());
static_assert(unicode_type<Type::UTF32>());

// Case changes (added 2026-11-24 by Claude Opus 5.5): the tables are searched and the text rebuilt while compiling,
// in every Type; Ascii with ASCII text only.
template <Type T>
[[nodiscard]] consteval bool ascii_case() {
    bool good = String<T>{"Hello, World"_js}.to_upper() == "HELLO, WORLD"_js && String<T>{"Hello, World"_js}.to_lower() == "hello, world"_js;
    good = good && String<T>{"the lord of the rings"_js}.to_headline() == "The Lord of the Rings"_js;
    good = good && String<T>{"don't stop"_js}.to_title() == "Don't Stop"_js;
    good = good && String<T>{"hi bob. bye"_js}.capitalize_sentences() == "Hi bob. Bye"_js;
    return good;
}
static_assert(ascii_case<Type::Ascii>());
static_assert(ascii_case<Type::UTF8Packed>());
static_assert(ascii_case<Type::UTF8Full>());
static_assert(ascii_case<Type::UTF16>());
static_assert(ascii_case<Type::UTF32>());

template <Type T>
[[nodiscard]] consteval bool unicode_case() {
    bool good = String<T>{"Straße"_js}.to_upper() == "STRASSE"_js;
    good = good && String<T>{"ΟΔΟΣ ΣΟΦΙΑ"_js}.to_lower() == "οδος σοφια"_js;
    good = good && String<T>{"ǆungla don't"_js}.to_title() == "ǅungla Don't"_js;
    good = good && String<T>{"¿qué? sí. bye"_js}.capitalize_sentences() == "¿Qué? Sí. Bye"_js;
    good = good && String<T>{"\U00010428"_js}.to_upper() == "\U00010400"_js;
    return good;
}
static_assert(unicode_case<Type::UTF8Packed>());
static_assert(unicode_case<Type::UTF8Full>());
static_assert(unicode_case<Type::UTF16>());
static_assert(unicode_case<Type::UTF32>());

// UTF-8 coming in, checked and repaired while compiling.
template <Type T>
[[nodiscard]] consteval bool utf8_in() {
    bool good = String<T>::from("\x80").error() == EncodingError{0} && String<T>::from("ok\xFF").error() == EncodingError{2};
    good = good && String<T>::from_lossy("a\xF0\x80\x80" "b") == "a���" "b"_js;
    good = good && !String<T>::from(std::string_view{"a\0b", 3})->to_c_string().has_value();
    jString::FixedString<T, 3> fixed;
    good = good && fixed.assign_lossy("\xC3\xA9\xFFxyz") == 3 && fixed == "é�x"_js;
    return good;
}
static_assert(utf8_in<Type::UTF8Packed>());
static_assert(utf8_in<Type::UTF8Full>());
static_assert(String<Type::Ascii>::from("caf\xC3\xA9").error() == EncodingError{3});
static_assert(String<Type::Ascii>::from_lossy("caf\xC3\xA9") == "caf?"_js);
static_assert(String<Type::UTF16>::from(u"\xD800").error() == EncodingError{0});

}  // namespace

int main() {
    std::cout << "every check passed while compiling\n";
    return 0;
}
