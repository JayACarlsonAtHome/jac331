// File: tests/test_string.cpp
// Purpose: String, FixedString and StringView at run time, for every Type: every way in and out, the promises in
//          string.hpp's header (moved-from is empty, appending to itself, to_c_string and zeros), comparing, searching,
//          joining, printing and hashing. The checks that need only ASCII run for all five Types; the ones with wider
//          code points for the four that hold them; then what is each Type's own (UTF8Full's cells, UTF16's pairs and
//          order, UTF32's units, Ascii refusing what is not ASCII). test_convert.cpp converts between them.
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <format>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#include "check.hpp"
#include "jString/v001/jstring.hpp"
#include "text_helpers.hpp"

namespace {

using jString::CodePointCount;
using jString::CodePointIndex;
using jString::CodePointNumber;
using jString::EncodingError;
using jString::FixedString;
using jString::String;
using jString::StringView;
using jString::Type;
using namespace jString::literals;
using jstr_test::name_of;
using jstr_test::packed;
using jstr_test::runtime_units;

// Every code point as its own piece of UTF-8, found by walking the bytes: the answer at() and substring() must match.
[[nodiscard]] std::vector<std::string_view> pieces_of(std::string_view units) {
    std::vector<std::string_view> all;
    std::size_t at = 0;
    while (at < units.size()) {
        const std::size_t length = jString::Utf8::scan(units.substr(at)).length;
        all.push_back(units.substr(at, length));
        at += length;
    }
    return all;
}

// at() and substring() at every position of text, against a walk of its UTF-8. Gives how many answers were wrong.
template <Type T, class Text>
[[nodiscard]] std::size_t wrong_answers(const Text& text) {
    const std::string bytes = packed<T>(text);
    const std::vector<std::string_view> pieces = pieces_of(bytes);
    std::size_t wrong = 0;
    if (text.count_code_points() != pieces.size()) ++wrong;
    std::size_t offset = 0;
    for (std::size_t n = 0; n < pieces.size(); ++n) {
        const auto one = text.at(CodePointIndex{n});
        if (!one || packed<T>(*one) != pieces[n] || one->count_code_points() != 1) ++wrong;
        const auto numbered = text.at(CodePointNumber{n + 1});
        if (!numbered || packed<T>(*numbered) != pieces[n]) ++wrong;
        const std::size_t take = std::min<std::size_t>(5, pieces.size() - n);
        std::size_t length = 0;
        for (std::size_t i = 0; i < take; ++i) length += pieces[n + i].size();
        const auto five = text.substring(CodePointIndex{n}, CodePointCount{take});
        if (!five || packed<T>(*five) != bytes.substr(offset, length) || five->count_code_points() != take) ++wrong;
        const auto rest = text.substring(CodePointIndex{n});
        if (!rest || packed<T>(*rest) != bytes.substr(offset)) ++wrong;
        offset += pieces[n].size();
    }
    if (!text.substring(CodePointIndex{pieces.size()}).has_value()) ++wrong;
    if (text.at(CodePointIndex{pieces.size()}).has_value()) ++wrong;
    if (text.substring(CodePointIndex{0}, CodePointCount{pieces.size() + 1}).has_value()) ++wrong;
    return wrong;
}

template <Type T>
[[nodiscard]] std::vector<char32_t> code_points_of(StringView<T> text) {
    std::vector<char32_t> all;
    for (const char32_t code_point : text.code_points()) all.push_back(code_point);
    return all;
}

// --- What every Type does, with ASCII text ---
template <Type T>
void every_type(jstr_test::Checker& check) {
    check.context(name_of(T));
    using Text = String<T>;
    using View = StringView<T>;

    // The ways in.
    const Text empty;
    check(empty.empty() && empty.count_code_points() == 0 && empty.size_utf8() == 0 && empty.to_utf8().empty(), "a new String is empty");
    const Text literal = "cafe"_js;
    check(packed<T>(literal) == "cafe" && literal.count_code_points() == 4 && literal.size_utf8() == 4, "a literal");
    Text source_text = "naive"_js;
    const Text copied{source_text.view()};
    source_text += "!"_js;
    check(copied == "naive"_js && source_text == "naive!"_js, "a String from a view owns a copy");
    const auto good = Text::from(runtime_units<T>("ok 5"));
    check(good.has_value() && *good == "ok 5"_js, "from() takes valid units");
    check(Text::from_lossy(runtime_units<T>("ok")) == "ok"_js && Text::from_lossy(runtime_units<T>("")).empty(), "lossy keeps valid text");

    // Moving and copying.
    Text source = "moved"_js;
    const Text moved_to{std::move(source)};
    check(moved_to == "moved"_js && source.empty() && source.count_code_points() == 0, "a String moved from is empty");
    Text assigned = "before"_js;
    Text giver = "after"_js;
    assigned = std::move(giver);
    check(assigned == "after"_js && giver.empty(), "move assignment: the giver is empty");
    Text self = "self"_js;
    Text& same = self;
    self = std::move(same);
    check(self == "self"_js, "moving a String onto itself keeps it");
    Text original = "first"_js;
    const Text duplicate = original;
    original += "!"_js;
    check(duplicate == "first"_js && original == "first!"_js, "a copy is its own text");

    // Copy assignment between text inside and text on the heap, each way round.
    const Text long_text = Text::from(runtime_units<T>("this text is longer than the sixty-four bytes held inside a String, in any Type")).value();
    Text was_on_heap = long_text;
    const Text short_text = "short"_js;
    was_on_heap = short_text;
    check(was_on_heap == "short"_js && wrong_answers<T>(was_on_heap) == 0, "copying short text onto long: the short text");
    was_on_heap += was_on_heap;
    check(was_on_heap == "shortshort"_js, "and it can still grow");
    Text was_inside = "x"_js;
    was_inside = long_text;
    check(was_inside == long_text && wrong_answers<T>(was_inside) == 0, "copying long text onto short");

    // Appending, joining.
    Text built;
    built += "one"_js;
    built.append(" two"_js).append(" three"_js);
    check(built == "one two three"_js, "append and += add at the end");
    Text twice = "ab"_js;
    twice += twice;
    twice.append(twice.view());
    check(twice == "abababab"_js, "appending a String or its view to itself doubles it");
    const Text a = "a"_js;
    const Text b = "b"_js;
    check(a + b == "ab"_js && a + "c"_js == "ac"_js && "c"_js + a == "ca"_js && View{"x"_js} + "y"_js == "xy"_js, "joining in each form");
    check(a == "a"_js && b == "b"_js, "joining changes neither side");

    // to_c_string and U+0000.
    const auto c = literal.to_c_string();
    check(c.has_value() && c->size() == 5 && c->back() == 0, "to_c_string ends with a zero after the text");
    check(empty.to_c_string().has_value() && empty.to_c_string()->size() == 1, "to_c_string of empty text is an empty C string");
    const auto with_zero = Text::from(runtime_units<T>(std::string_view{"a\0x", 3}));
    check(with_zero.has_value() && with_zero->count_code_points() == 3 && !with_zero->to_c_string().has_value(), "a zero is text, but no C string");
    const View zero = *with_zero->at(CodePointIndex{1});
    const auto around_zero = with_zero->split_once(zero);
    check(zero == U'\0' && around_zero.has_value() && around_zero->first == "a"_js && around_zero->second == "x"_js, "U+0000 is found where it is");
    check(!Text{"ax"_js}.contains(zero), "and only there");

    // Comparing.
    check(Text{"abc"_js} == View{"abc"_js} && View{"abc"_js} == Text{"abc"_js} && View{"abc"_js} != View{"abd"_js}, "equal both ways, and unequal");
    check(View{"a"_js} < View{"b"_js} && View{"ab"_js} > View{"a"_js} && View{"\x7E"_js} < View{"\x7F"_js}, "ordered");

    // Searching.
    const View path = "dir/file.txt"_js;
    check(path.starts_with("dir/"_js) && path.ends_with(".txt"_js) && path.contains("file"_js), "starts, ends, contains");
    check(!path.starts_with("file"_js) && !path.ends_with("dir"_js) && !path.contains("dirt"_js), "and the misses");
    const auto parts = path.split_once("/"_js);
    check(parts.has_value() && parts->first == "dir"_js && parts->second == "file.txt"_js, "split at the first separator");
    check(!path.split_once("|"_js).has_value() && !path.split_once(""_js).has_value(), "nothing for a missing or an empty separator");

    // Positions, pieces, replacing.
    const Text data = "This is a test"_js;
    check(data.at(CodePointIndex{6}) == U's' && data.at(CodePointIndex{6}) == "s"_js && data.at(CodePointNumber{8}) == U' ', "one code point, counted from 0 or 1");
    check(!data.at(CodePointIndex{14}).has_value() && !data.at(CodePointNumber{0}).has_value() && !empty.at(CodePointIndex{0}).has_value(), "nothing past the end, or 0th");
    check(data.substring(CodePointNumber{1}, CodePointCount{4}) == "This"_js && data.substring(CodePointIndex{10}) == "test"_js, "pieces");
    check(data.substring(CodePointIndex{14}) == ""_js && !data.substring(CodePointIndex{15}).has_value(), "just past the end is empty text, further is nothing");
    check(!data.substring(CodePointIndex{11}, CodePointCount{4}).has_value(), "a piece running past the end is nothing");
    check(data.replace(CodePointNumber{1}, CodePointCount{4}, "That"_js) == "That is a test"_js && data == "This is a test"_js, "replace makes a new String");
    check(data.replace(CodePointIndex{14}, CodePointCount{0}, "s"_js) == "This is a tests"_js && data.replace(CodePointIndex{0}, CodePointCount{5}, ""_js) == "is a test"_js,
          "appending and removing");
    check(!data.replace(CodePointIndex{12}, CodePointCount{3}, "x"_js).has_value() && View{"abc"_js}.replace(CodePointNumber{2}, CodePointCount{1}, "x"_js) == "axc"_js,
          "nothing past the end; and from a view");
    check(!(View{"ab"_js} == U'a') && !(View{} == U'a'), "== a code point means exactly that one code point");

    // Inside and on the heap: the same answers at every position.
    const std::string ascii_bytes(200, 'q');
    check(wrong_answers<T>(Text::from(runtime_units<T>(ascii_bytes)).value()) == 0, "ASCII on the heap");
    check(wrong_answers<T>(String<T, 0>::from(runtime_units<T>("all on the heap")).value()) == 0, "every text on the heap");
    Text grown;
    std::size_t growing_wrong = 0;
    for (int i = 0; i < 40; ++i) {
        grown += "abc"_js;
        growing_wrong += wrong_answers<T>(grown);
    }
    check(growing_wrong == 0 && grown.count_code_points() == 120, "append: inside to heap, at every step");
    Text with_room;
    with_room.reserve_code_points(100);
    with_room += "0123456789"_js;
    with_room += with_room;
    with_room += with_room;
    check(with_room.count_code_points() == 40 && wrong_answers<T>(with_room) == 0, "appending to itself where there is room already");
    Text heap_source = long_text;
    Text heap_target = "x"_js;
    heap_target = std::move(heap_source);
    check(heap_target == long_text && heap_source.empty(), "moving text on the heap leaves the source empty");
    heap_source += "again"_js;
    check(heap_source == "again"_js, "and it can be used again");

    // FixedString: all inside, no destructor, all zeros is empty.
    using Payload = FixedString<T, 8>;
    static_assert(std::is_trivially_copyable_v<Payload> && std::is_trivially_destructible_v<Payload>);
    const auto from_zeros = std::bit_cast<Payload>(std::array<std::byte, sizeof(Payload)>{});
    check(from_zeros.empty() && from_zeros == ""_js, "all zeros is the empty text");
    Payload payload;
    check(payload.assign_lossy(runtime_units<T>("0123456789")) == 8 && payload == "01234567"_js,
          "assign_lossy cuts after 8 code points");
    check(Payload::from("short"_js).has_value() && !Payload::from("nine code"_js).has_value(), "from(): whole, or nothing when it does not fit");
    payload.clear();
    check(payload.empty() && payload == ""_js, "clear() empties it");

    // Code points, printing, hashing.
    check(code_points_of<T>("az"_js) == std::vector<char32_t>{U'a', U'z'} && code_points_of<T>(""_js).empty(), "code points in order");
    std::ostringstream out;
    out << literal << '|' << View{"ab"_js} << '|' << Payload::from_lossy(runtime_units<T>("p"));
    check(out.str() == "cafe|ab|p", "printed to a stream as UTF-8");
    check(std::format("[{}] [{:>4}]", literal, View{"ab"_js}) == "[cafe] [  ab]", "std::format, with options");
    std::unordered_set<Text> seen;
    seen.insert("x"_js);
    seen.insert(Text{"x"_js});
    seen.insert("y"_js);
    check(seen.size() == 2 && seen.contains("x"_js), "hashes alike for equal text");
    check(sizeof(View) == 2 * sizeof(std::size_t), "a view is a pointer and a size");
}

// --- What the four Unicode Types do with code points of every width ---
template <Type T>
void unicode_types(jstr_test::Checker& check) {
    check.context(name_of(T));
    using Text = String<T>;
    using View = StringView<T>;

    const Text literal = "café"_js;
    check(packed<T>(literal) == "caf\xC3\xA9" && literal.count_code_points() == 4 && literal.size_utf8() == 5, "a literal, as UTF-8");
    const Text widths = "aé€\U0001F600"_js;
    check(widths.count_code_points() == 4 && packed<T>(widths) == "a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80", "a code point of each width");
    std::array<char, 9> room{};
    check(widths.write_utf8(room) == std::nullopt && room == std::array<char, 9>{}, "write_utf8 writes nothing into too little room");
    std::array<char, 12> enough{};
    check(widths.write_utf8(enough) == std::optional<std::size_t>{10} && std::string_view{enough.data(), 10} == packed<T>(widths), "and the bytes into enough");

    check(Text::from(runtime_units<T>("\xE2\x82\xAC 5")).value() == "€ 5"_js, "from() takes valid units of every width");
    check(Text::from_lossy(runtime_units<T>("é")) == "é"_js, "and so does from_lossy");

    // Comparing in code point order, across widths: U+FFFF before U+10000 although UTF-16 keeps U+10000 in units
    // D800 DC00, below FFFF.
    check(View{"ÿ"_js} < View{"Ā"_js} && View{"z"_js} < View{"é"_js} && View{"\x7F"_js} < View{"\u0080"_js}, "code point order");
    check(View{"｡"_js} < View{"\U00010000"_js} && View{"￿"_js} < View{"\U0010FFFF"_js} && View{""_js} < View{"\U00010000"_js},
          "above the surrogates' place, below a pair");
    check(View{"퟿"_js} < View{""_js} && View{"a\U00010000"_js} > View{"a￿"_js}, "and around them");

    // Searching mixed text: a whole code point, never part of one.
    check(View{"café été"_js}.contains("ét"_js) && !View{"café"_js}.contains("e"_js), "searching mixed text");
    check(View{"a\U0001F600b"_js}.split_once("\U0001F600"_js)->second == "b"_js, "split at a code point of two UTF-16 units");

    // Positions past wide code points.
    const Text accented = "Thé is a test"_js;
    check(accented.at(CodePointIndex{5}) == U's' && accented.at(CodePointIndex{2}) == U'é' && accented.at(CodePointIndex{2})->size_utf8() == 2,
          "an index counts code points, not units");
    const View emoji = "a\U0001F600b"_js;
    check(emoji.at(CodePointIndex{1}) == U'\U0001F600' && emoji.at(CodePointIndex{2}) == U'b', "past a four-byte code point");
    check(accented.substring(CodePointIndex{2}, CodePointCount{3}) == "é i"_js, "a piece that starts with one");
    check(accented.replace(CodePointNumber{3}, CodePointCount{1}, "e"_js) == "The is a test"_js && View{"abc"_js}.replace(CodePointNumber{2}, CodePointCount{1}, "é"_js) == "aéc"_js,
          "replacing with and without wide code points");

    // Mixed text at every position, inside and on the heap, and through every way it can come in.
    const std::string mixed_bytes = "aé日\U0001F600 the café naïve über fox \U0001F600\U0001F600 end. and more ASCII to pass sixteen units at a step, then 日本語.";
    const auto mixed = Text::from(runtime_units<T>(mixed_bytes));
    check(mixed.has_value() && packed<T>(*mixed) == mixed_bytes && wrong_answers<T>(*mixed) == 0 && wrong_answers<T>(mixed->view()) == 0, "mixed text: every position");
    check(wrong_answers<T>(String<T, 0>::from(runtime_units<T>(mixed_bytes)).value()) == 0, "every text on the heap");
    check(wrong_answers<T>(Text{"aé日\U0001F600 inside"_js}) == 0 && wrong_answers<T>(Text{""_js}) == 0, "short and empty text");
    std::string cjk;
    for (int i = 0; i < 40; ++i) cjk += "日本語";
    const std::string late_wide = std::string(100, 'a') + "\U0001F600" + std::string(100, 'a');
    check(wrong_answers<T>(Text::from(runtime_units<T>(cjk)).value()) == 0 && wrong_answers<T>(Text::from(runtime_units<T>(late_wide)).value()) == 0,
          "three-byte code points; one wide one after 100 ASCII");
    check(packed<T>(Text::from_lossy(runtime_units<T>(late_wide))) == late_wide, "from_lossy of valid text is the text");

    String<T, 16> grown;
    std::size_t growing_wrong = 0;
    for (const View piece : {View{"ab"_js}, View{"c"_js}, View{"é"_js}, View{"\U0001F600xyz"_js}, View{"0123456789abcdef0123"_js}, View{"日"_js}}) {
        grown += piece;
        growing_wrong += wrong_answers<T>(grown);
    }
    check(growing_wrong == 0 && grown == "abcé\U0001F600xyz0123456789abcdef0123日"_js && grown.count_code_points() == 29, "append across the move to the heap");
    String<T, 16> doubled{"éa"_js};
    doubled += doubled;
    doubled += doubled;
    doubled += doubled;
    check(doubled == "éaéaéaéaéaéaéaéa"_js && wrong_answers<T>(doubled) == 0, "appending to itself, across the move to the heap");
    const Text joined = *mixed + *mixed;
    check(packed<T>(joined) == mixed_bytes + mixed_bytes && wrong_answers<T>(joined) == 0 && *mixed < joined, "joining");
    const auto wide = mixed->replace(CodePointIndex{3}, CodePointCount{2}, "ééé"_js);
    check(wide.has_value() && wrong_answers<T>(*wide) == 0 && wide->count_code_points() == mixed->count_code_points() + 1, "replace keeps the count");
    std::ostringstream long_out;
    long_out << *mixed << *mixed << *mixed;
    check(long_out.str() == mixed_bytes + mixed_bytes + mixed_bytes, "text longer than the piece printed at a time");

    // U+0000 after a wide code point.
    const auto with_zero = Text::from(runtime_units<T>(std::string_view{"\xC3\xA9\0x", 4}));
    const View zero = *with_zero->at(CodePointIndex{1});
    check(with_zero->split_once(zero)->first == "é"_js && !Text{"éx"_js}.contains(zero), "U+0000 only where it is, after é");

    // FixedString with wide code points: room for the widest.
    using Payload = FixedString<T, 8>;
    Payload payload;
    check(payload.assign_lossy(runtime_units<T>("café")) == 4 && payload == "café"_js, "assign_lossy keeps valid text");
    check(payload.assign_lossy(runtime_units<T>("\U0001F600\U0001F600\U0001F600\U0001F600\U0001F600\U0001F600\U0001F600\U0001F600\U0001F600")) == 8 &&
              payload.view().size_utf8() == 32 && wrong_answers<T>(payload.view()) == 0,
          "eight of the widest code points fill it");
    check(Payload::from_lossy(runtime_units<T>("ab")) == "ab"_js, "written over longer text with nothing cleared");
    std::ostringstream fixed_out;
    fixed_out << Payload::from_lossy(runtime_units<T>("é")) << std::format("{:>4}", Payload::from_lossy(runtime_units<T>("ü")));
    check(fixed_out.str() == "\xC3\xA9   \xC3\xBC", "printed and formatted as UTF-8");

    check(code_points_of<T>("aé€\U0001F600"_js) == std::vector<char32_t>{U'a', U'é', U'€', U'\U0001F600'}, "code points of each width, in order");
}

// --- UTF-8 coming in, for the two UTF-8 Types: where bad bytes are found, and what lossy makes of them ---
template <Type T>
void utf8_in(jstr_test::Checker& check) {
    check.context(name_of(T));
    using Text = String<T>;
    const auto broken = Text::from(std::string{"price \xE2\x82 5"});
    check(!broken.has_value() && broken.error() == EncodingError{6}, "from() says where the bytes go wrong");
    check(!Text::from(std::string{"\xFF"}).has_value(), "from() refuses a byte that is never UTF-8");
    check(Text::from_lossy(std::string{"a\x80" "b"}) == "a�b"_js, "a lone continuation byte");
    check(Text::from_lossy(std::string{"\xF0\x80\x80"}) == "���"_js, "F0 80 80: three bad bytes, three U+FFFD");
    check(Text::from_lossy(std::string{"\xE1\x80" "A"}) == "�" "A"_js, "a cut sequence is one U+FFFD");
    const std::string late_wide = std::string(100, 'a') + "\U0001F600" + std::string(100, 'a');
    const auto bad_late = Text::from(late_wide + "\xFF");
    check(!bad_late.has_value() && bad_late.error() == EncodingError{late_wide.size()}, "bad text after a long valid run");
    check(packed<T>(Text::from_lossy(late_wide + "\xFF")) == late_wide + "�", "and lossy, a U+FFFD where it was");
    FixedString<T, 8> payload;
    check(payload.assign_lossy(std::string{"\xC3\xA9\xFF\U0001F600"}) == 3 && payload == "é�\U0001F600"_js, "a bad byte made U+FFFD in a FixedString");
}

}  // namespace

int main() {
    jstr_test::Checker check;

    every_type<Type::Ascii>(check);
    every_type<Type::UTF8Packed>(check);
    every_type<Type::UTF8Full>(check);
    every_type<Type::UTF16>(check);
    every_type<Type::UTF32>(check);
    unicode_types<Type::UTF8Packed>(check);
    unicode_types<Type::UTF8Full>(check);
    unicode_types<Type::UTF16>(check);
    unicode_types<Type::UTF32>(check);
    utf8_in<Type::UTF8Packed>(check);
    utf8_in<Type::UTF8Full>(check);

    // --- Sizes: a String is one cache line inside, a vector and a size; a FixedString keeps room for its widest code
    //     points and a size ---
    check.context("sizes: ");
    static_assert(sizeof(String<Type::Ascii>) == sizeof(String<Type::UTF32>) && sizeof(String<Type::UTF8Full>) == 64 + sizeof(std::vector<char>) + sizeof(std::size_t));
    static_assert(sizeof(FixedString<Type::Ascii, 8>) == 8 + sizeof(std::size_t));
    static_assert(sizeof(FixedString<Type::UTF8Packed, 8>) == 8 * 4 + sizeof(std::size_t) && sizeof(FixedString<Type::UTF8Full, 8>) == 8 * 4 + sizeof(std::size_t));
    static_assert(sizeof(FixedString<Type::UTF16, 8>) == 8 * 4 + sizeof(std::size_t) && sizeof(FixedString<Type::UTF32, 8>) == 8 * 4 + sizeof(std::size_t));
    check(String<Type::Ascii>::inline_units == 64 && String<Type::UTF16>::inline_units == 32 && String<Type::UTF32>::inline_units == 16, "units held inside");

    // --- Ascii: ASCII in, nothing else; lossy reads UTF-8, so a wide code point is one '?' ---
    check.context("Ascii: ");
    using Plain = String<Type::Ascii>;
    const auto refused = Plain::from(std::string{"caf\xC3\xA9"});
    check(!refused.has_value() && refused.error() == EncodingError{3}, "from() refuses a byte above 7F, and says where");
    check(Plain::from_lossy(std::string{"caf\xC3\xA9 \U0001F600"}) == "caf? ?"_js, "lossy: one '?' for each code point above U+007F");
    check(Plain::from_lossy(std::string{"a\xFF" "b"}) == "a?b"_js, "and for each bad sequence");
    FixedString<Type::Ascii, 3> short_plain;
    check(short_plain.assign_lossy(std::string{"\xC3\xA9t\xC3\xA9s"}) == 3 && short_plain == "?t?"_js, "a FixedString counts the '?' as one each");
    check(StringView<Type::Ascii>{"z"_js} < StringView<Type::Ascii>{"\x7F"_js}, "ordered as bytes");

    // --- UTF8Full: a code point to a 4-byte cell, its bytes then zeros, the same on every machine ---
    check.context("UTF8Full: ");
    static_assert(jString::Utf8::cell_units == 4);
    static_assert(std::is_same_v<jstr::String<jstr::Type::UTF16>, jString::String<jString::Type::UTF16>>, "jstr is the short name of jString");
    static_assert(std::is_same_v<jstr_vw<jstr::Type::UTF8Packed>, jString::StringView<jString::Type::UTF8Packed>>, "jstr_vw is the short name of StringView");
    const auto cells = String<Type::UTF8Full>::from(std::string_view{"\xC3\xA9\0x", 4});
    check(cells->count_code_points() == 3 && cells->size_packed() == 4, "three cells, four bytes packed");
    check(!String<Type::UTF8Full>{"éx"_js}.contains(*cells->at(CodePointIndex{1})), "U+0000 never matched in the zeros after é");

    // --- UTF16: pairs for code points above U+FFFF, and a surrogate anywhere else is bad ---
    check.context("UTF16: ");
    using Wide = String<Type::UTF16>;
    check(Wide{"\U0001F600"_js}.size_packed() == 2 && Wide{"\U0001F600"_js}.to_packed() == std::vector<char16_t>{u'\xD83D', u'\xDE00'}, "U+1F600 is the pair D83D DE00");
    check(Wide{"é"_js}.to_packed() == std::vector<char16_t>{u'é'} && Wide{"é"_js}.to_c_string() == std::vector<char16_t>{u'é', u'\0'}, "packed, and as a C string");
    const auto lone_high = Wide::from(std::u16string{u'a', u'\xD83D', u'b'});
    check(!lone_high.has_value() && lone_high.error() == EncodingError{1}, "a high surrogate with no low one after it");
    const auto lone_low = Wide::from(std::u16string{u'\xDE00'});
    check(!lone_low.has_value() && lone_low.error() == EncodingError{0}, "a low surrogate first");
    check(!Wide::from(std::u16string{u'\xD83D'}).has_value(), "a high surrogate at the end");
    check(Wide::from_lossy(std::u16string{u'a', u'\xD83D', u'b', u'\xDE00'}) == "a�b�"_js, "lossy: each lone surrogate is one U+FFFD");
    const Wide paired = "a\U0001F600"_js;
    check(paired.count_code_points() == 2 && paired.at(CodePointIndex{1}) == U'\U0001F600', "a pair is one code point");

    // --- UTF32: the code point itself, and nothing that is not one ---
    check.context("UTF32: ");
    using Full = String<Type::UTF32>;
    check(Full{"é\U0001F600"_js}.to_packed() == std::vector<char32_t>{U'é', U'\U0001F600'}, "each unit is its code point");
    const auto surrogate = Full::from(std::u32string{U'a', U'\xD800'});
    check(!surrogate.has_value() && surrogate.error() == EncodingError{1}, "a surrogate is not a code point");
    check(!Full::from(std::u32string{static_cast<char32_t>(0x110000)}).has_value(), "nor is anything past U+10FFFF");
    check(Full::from_lossy(std::u32string{U'a', static_cast<char32_t>(0x110000), U'b'}) == "a�b"_js, "lossy: U+FFFD for each");
    return check.finish();
}
