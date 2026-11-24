// File: tests/test_case.cpp
// Purpose: String's case changes: to_lower, to_upper, to_title, to_headline and capitalize_sentences, for every Type.
//          Each makes a new String and leaves the original as it was; text can grow (ß to SS); capital sigma ends a
//          word as final sigma; words and sentences start where case_mapping.hpp says. The checks that need only
//          ASCII run for all five Types, the rest for the four that hold every code point. Every code point is also
//          checked against Python by tools/verify_case_with_python.py. Written 2026-11-22 by Claude Opus 5.5.
// Modified 2026-11-24 by Claude Opus 5.5: every Type, each made from units at run time.
#include <cstddef>
#include <string>
#include <string_view>

#include "check.hpp"
#include "jString/v001/jstring.hpp"
#include "text_helpers.hpp"

namespace {

using jString::CodePointCount;
using jString::CodePointIndex;
using jString::String;
using jString::Type;
using jstr_test::name_of;
using jstr_test::packed;
using jstr_test::runtime_units;

// Text built at run time, so the compiler cannot check it.
template <Type T>
[[nodiscard]] String<T> made(std::string_view utf8) {
    return *String<T>::from(runtime_units<T>(utf8));
}

// Each change of UTF-8 text, made in T and read back as UTF-8.
template <Type T>
[[nodiscard]] std::string lower(std::string_view utf8) {
    return packed<T>(made<T>(utf8).to_lower());
}
template <Type T>
[[nodiscard]] std::string upper(std::string_view utf8) {
    return packed<T>(made<T>(utf8).to_upper());
}
template <Type T>
[[nodiscard]] std::string title(std::string_view utf8) {
    return packed<T>(made<T>(utf8).to_title());
}
template <Type T>
[[nodiscard]] std::string headline(std::string_view utf8) {
    return packed<T>(made<T>(utf8).to_headline());
}
template <Type T>
[[nodiscard]] std::string sentences(std::string_view utf8) {
    return packed<T>(made<T>(utf8).capitalize_sentences());
}

// --- Every Type: ASCII text only ---

template <Type T>
void ascii_text(jstr_test::Checker& check) {
    const String<T> name = made<T>("the CAT in the hat. it sat");
    const String<T> shouted = name.to_upper();
    check(packed<T>(name) == "the CAT in the hat. it sat", "the original is not changed");
    check(packed<T>(shouted) == "THE CAT IN THE HAT. IT SAT", "upper");
    check(shouted.count_code_points() == name.count_code_points(), "ASCII never grows");
    check(lower<T>("Hello, World 123!") == "hello, world 123!", "lower");
    check(upper<T>("Hello, World 123!") == "HELLO, WORLD 123!", "upper, with digits and punctuation kept");
    check(title<T>("the lord of the rings") == "The Lord Of The Rings", "title: every word");
    check(title<T>("tHE LORD") == "The Lord", "title: the rest of each word lowered");
    check(title<T>("don't stop") == "Don't Stop", "an apostrophe inside a word does not end it");
    check(title<T>("21st century") == "21st Century", "a word starting with a number is lowered after it");
    check(title<T>("hello-world foo_bar") == "Hello-World Foo_bar", "a hyphen ends a word; an underscore does not");
    check(title<T>("'tis \"quoted\" (here)") == "'Tis \"Quoted\" (Here)", "punctuation around words is kept");
    check(headline<T>("the lord of the rings") == "The Lord of the Rings", "small words lowered, the first not");
    check(headline<T>("WHAT IT IS FOR") == "What It Is For", "the last word is raised even when small");
    check(headline<T>("star wars: a new hope") == "Star Wars: A New Hope", "a word after a colon is raised");
    check(headline<T>("one for all and all for one") == "One for All and All for One", "for, and");
    check(headline<T>("a") == "A", "one small word is the first and the last");
    check(headline<T>("THE CAT IN THE HAT") == "The Cat in the Hat", "from upper case");
    check(sentences<T>("hello Bob. bye") == "Hello Bob. Bye", "only the starts are raised");
    check(sentences<T>("hello WORLD. iPhone") == "Hello WORLD. IPhone", "nothing else is changed");
    check(sentences<T>("what? yes! no.") == "What? Yes! No.", "? and ! end sentences");
    check(sentences<T>("he said \"stop.\" then left") == "He said \"stop.\" Then left", "closing quote after the stop");
    check(sentences<T>("(note) see. (also) this") == "(Note) see. (Also) this", "opening punctuation before the first letter is passed over");
    check(sentences<T>("3 apples. two") == "3 apples. Two", "a sentence that starts with a number is left as it is");
    check(sentences<T>("one.two") == "One.two", "no space after the stop: not a new sentence");
    check(sentences<T>("first line.\nsecond line") == "First line.\nSecond line", "a line break is white space");
    check(sentences<T>("e.g. this") == "E.g. This", "an abbreviation is taken for a sentence's end (as documented)");
    check(String<T>{}.to_upper().empty() && String<T>{}.to_title().empty() && String<T>{}.capitalize_sentences().empty(), "empty stays empty");
}

// --- The four Types that hold every code point ---

template <Type T>
void originals_stay(jstr_test::Checker& check) {
    const String<T> name = made<T>("ǆemal Straße");
    const String<T> lowered = name.to_lower();
    const String<T> raised = name.to_upper();
    const String<T> titled = name.to_title();
    check(packed<T>(name) == "ǆemal Straße", "the original is not changed by any of them");
    check(packed<T>(lowered) == "ǆemal straße" && packed<T>(raised) == "ǄEMAL STRASSE" && packed<T>(titled) == "ǅemal Straße",
          "each change is a new String");
    check(headline<T>("ǆemal Straße") == "ǅemal Straße" && sentences<T>("ǆemal Straße") == "ǅemal Straße", "headline and sentences too");
}

template <Type T>
void lower_and_upper(jstr_test::Checker& check) {
    check(lower<T>("ÀÉÎÕÜ Ñ") == "àéîõü ñ", "Latin-1 lower");
    check(upper<T>("straße") == "STRASSE", "ß upper is SS: the text grows");
    check(made<T>("straße").to_upper().count_code_points() == 7, "and its count is right");
    check(upper<T>("ﬁne") == "FINE", "the ﬁ ligature upper is two letters");
    check(upper<T>("ŉ") == "ʼN", "ŉ upper is ʼN");
    check(lower<T>("İ") == "i̇", "capital I with dot lowers to i and a combining dot, in every language");
    check(upper<T>("\u03B0") == "\u03A5\u0308\u0301", "one Greek letter upper is three code points: Υ and two combining marks");
    check(lower<T>("ᲐᲑᲒ") == "აბგ", "Georgian Mtavruli lowers to Mkhedruli");
    check(lower<T>("𐐀𐐁") == "𐐨𐐩", "Deseret, outside the first 64 Ki code points (UTF16's pairs)");
    check(upper<T>("𐐨𐐩") == "𐐀𐐁", "and back");
    check(upper<T>("日本語 123") == "日本語 123", "text with no case is unchanged");
    check(upper<T>("ẞ") == "ẞ" && lower<T>("ẞ") == "ß", "capital sharp s stays, and lowers to ß");
}

template <Type T>
void final_sigma(jstr_test::Checker& check) {
    check(lower<T>("ΟΔΟΣ") == "οδος", "a word-final capital sigma lowers to final sigma");
    check(lower<T>("ΣΟΦΙΑ") == "σοφια", "a word-initial one to sigma");
    check(lower<T>("ΟΔΟΣ ΟΔΟΣ.") == "οδος οδος.", "before a space and before a full stop: final");
    check(lower<T>("Σ") == "σ", "alone: not final (nothing cased before it)");
    check(lower<T>("ΑΣ'Α") == "ασ'α", "an apostrophe is case-ignorable, so a letter after it means not final");
    check(lower<T>("ΑΣ'") == "ας'", "and with nothing cased after it, final");
    check(title<T>("ΟΔΟΣ ΣΟΦΙΑ") == "Οδος Σοφια", "final sigma inside title case");
}

template <Type T>
void wide_words(jstr_test::Checker& check) {
    check(title<T>("ǆungla") == "ǅungla", "ǆ has its own title form, ǅ");
    check(title<T>("ﬁsh") == "Fish", "the ﬁ ligature title-cases to Fi");
    check(title<T>("ⓐⓑ ⓒ") == "Ⓐⓑ Ⓒ", "circled letters are cased, so they make words");
    check(sentences<T>("¿qué? ¡sí!") == "¿Qué? ¡Sí!", "Spanish opening marks");
    check(sentences<T>("ǆ is a letter. ǆ again") == "ǅ is a letter. ǅ again", "title form at a sentence start");
}

template <Type T>
void long_text(jstr_test::Checker& check) {
    std::string many;
    for (int i = 0; i < 200; ++i) many += "straße ΟΔΟΣ ";
    const String<T> text = made<T>(many);
    const String<T> raised = text.to_upper();
    check(text.count_code_points() == 200 * 12, "the original: 12 code points a piece");
    check(raised.count_code_points() == 200 * 13, "long text on the heap: each ß became two letters");
    const auto piece = raised.substring(CodePointIndex{13}, CodePointCount{7});
    check(piece.has_value() && packed<T>(*piece) == "STRASSE", "and it is indexed right");
    const String<T> back = raised.to_lower();
    const auto start = back.substring(CodePointIndex{0}, CodePointCount{12});
    check(start.has_value() && packed<T>(*start) == "strasse οδος", "and back, with the final sigma");
}

template <Type T>
void every_type(jstr_test::Checker& check) {
    check.context(name_of(T));
    ascii_text<T>(check);
    if constexpr (T != Type::Ascii) {
        originals_stay<T>(check);
        lower_and_upper<T>(check);
        final_sigma<T>(check);
        wide_words<T>(check);
        long_text<T>(check);
    }
}

// The ASCII shortcuts in case_mapping.hpp must give exactly what the Unicode tables give.
void ascii_shortcuts(jstr_test::Checker& check) {
    check.context("");
    namespace c = jString::detail::casing;
    namespace t = jString::detail::case_tables;
    int wrong = 0;
    for (char32_t cp = 0; cp < 0x80; ++cp) {
        wrong += c::is_cased(cp) != c::in(t::cased, cp) ? 1 : 0;
        wrong += c::is_case_ignorable(cp) != c::in(t::case_ignorable, cp) ? 1 : 0;
        wrong += c::is_word(cp) != (c::in(t::word, cp) || c::in(t::cased, cp)) ? 1 : 0;
        wrong += c::is_white_space(cp) != c::in(t::white_space, cp) ? 1 : 0;
        wrong += c::is_sentence_terminal(cp) != c::in(t::sentence_terminal, cp) ? 1 : 0;
        wrong += c::is_sentence_opening(cp) != c::in(t::sentence_opening, cp) ? 1 : 0;
        wrong += c::is_sentence_closing(cp) != c::in(t::sentence_closing, cp) ? 1 : 0;
    }
    check(wrong == 0, "every ASCII shortcut agrees with its table");
}

}  // namespace

int main() {
    jstr_test::Checker check;
    every_type<Type::Ascii>(check);
    every_type<Type::UTF8Packed>(check);
    every_type<Type::UTF8Full>(check);
    every_type<Type::UTF16>(check);
    every_type<Type::UTF32>(check);
    ascii_shortcuts(check);
    return check.finish();
}
