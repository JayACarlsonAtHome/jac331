// File: tests/test_string.cpp
// Purpose: String, FixedString and StringView at run time: every way in and out, the promises in string.hpp's header
//          (moved-from is empty, appending to itself, to_c_string and zeros), the cells themselves (a code point to a
//          cell, compared as bytes, U+0000 never matched in a cell's padding), comparing, searching, joining, printing
//          and hashing.
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

namespace {

using jstr::EncodingError;
using jstr::FixedString;
using jstr::String;
using jstr::StringView;
using namespace jstr::literals;

// Text built at run time, so the compiler cannot check it: the way bytes from a file or a C library arrive.
[[nodiscard]] std::string runtime_bytes(std::string_view bytes) { return std::string{bytes}; }

// Text packed again, as a std::string, to compare with the bytes it was made from.
[[nodiscard]] std::string packed(StringView<> text) {
    const std::vector<char> units = text.to_utf8();
    return std::string{units.begin(), units.end()};
}

// Every code point as its own piece of packed text, found by walking the units: the answer at() and substring() must
// match.
[[nodiscard]] std::vector<std::string_view> pieces_of(std::string_view units) {
    std::vector<std::string_view> all;
    std::size_t at = 0;
    while (at < units.size()) {
        const std::size_t length = jstr::Utf8::scan(units.substr(at)).length;
        all.push_back(units.substr(at, length));
        at += length;
    }
    return all;
}

// at() and substring() at every position of text, against a walk of the bytes it holds. Gives how many answers were
// wrong.
template <class Text>
[[nodiscard]] std::size_t wrong_answers(const Text& text) {
    const std::string bytes = packed(text);
    const std::vector<std::string_view> pieces = pieces_of(bytes);
    std::size_t wrong = 0;
    if (text.count_code_points() != pieces.size()) ++wrong;
    std::size_t offset = 0;
    for (std::size_t n = 0; n < pieces.size(); ++n) {
        const auto one = text.at(jstr::CodePointIndex{n});
        if (!one || packed(*one) != pieces[n] || one->count_code_points() != 1) ++wrong;
        const auto numbered = text.at(jstr::CodePointNumber{n + 1});
        if (!numbered || packed(*numbered) != pieces[n]) ++wrong;
        const std::size_t take = std::min<std::size_t>(5, pieces.size() - n);
        std::size_t length = 0;
        for (std::size_t i = 0; i < take; ++i) length += pieces[n + i].size();
        const auto five = text.substring(jstr::CodePointIndex{n}, jstr::CodePointCount{take});
        if (!five || packed(*five) != bytes.substr(offset, length) || five->count_code_points() != take) ++wrong;
        const auto rest = text.substring(jstr::CodePointIndex{n});
        if (!rest || packed(*rest) != bytes.substr(offset)) ++wrong;
        offset += pieces[n].size();
    }
    if (!text.substring(jstr::CodePointIndex{pieces.size()}).has_value()) ++wrong;
    if (text.at(jstr::CodePointIndex{pieces.size()}).has_value()) ++wrong;
    if (text.substring(jstr::CodePointIndex{0}, jstr::CodePointCount{pieces.size() + 1}).has_value()) ++wrong;
    return wrong;
}

[[nodiscard]] std::vector<char32_t> code_points_of(StringView<> text) {
    std::vector<char32_t> all;
    for (const char32_t code_point : text.code_points()) all.push_back(code_point);
    return all;
}

}  // namespace

int main() {
    jstr_test::Checker check;

    // --- The ways in ---
    const String<> empty;
    check(empty.empty() && empty.count_code_points() == 0 && empty.size_utf8() == 0 && empty.to_utf8().empty(), "a new String is empty");
    const String<> literal = "café"_js;
    check(packed(literal) == "caf\xC3\xA9" && literal.size_utf8() == 5, "a literal, checked while compiling, packs to its bytes");
    check(literal.count_code_points() == 4, "four code points in five bytes");
    const StringView<> view = "naïve"_js;
    String<> source_text = "naïve"_js;
    const String<> copied{source_text.view()};
    source_text += "!"_js;
    check(copied == view && source_text == "naïve!"_js, "a String from a view owns a copy");

    const auto good = String<>::from(runtime_bytes("\xE2\x82\xAC 5"));
    check(good.has_value() && *good == "€ 5"_js, "from() takes valid bytes");
    const auto broken = String<>::from(runtime_bytes("price \xE2\x82 5"));
    check(!broken.has_value() && broken.error() == EncodingError{6}, "from() says where the bytes go wrong");
    check(!String<>::from(runtime_bytes("\xFF")).has_value(), "from() refuses a byte that is never UTF-8");

    // from_lossy: each maximal subpart becomes one U+FFFD.
    check(String<>::from_lossy(runtime_bytes("ok")) == "ok"_js, "lossy keeps valid text as it is");
    check(String<>::from_lossy(runtime_bytes("a\x80" "b")) == "a�b"_js, "a lone continuation byte");
    check(String<>::from_lossy(runtime_bytes("\xF0\x80\x80")) == "���"_js, "F0 80 80: three bad bytes, three U+FFFD");
    check(String<>::from_lossy(runtime_bytes("\xE1\x80" "A")) == "�" "A"_js, "a cut sequence is one U+FFFD");
    check(String<>::from_lossy(runtime_bytes("")).empty(), "lossy of nothing is empty");

    // --- The cells: a code point to a cell, its bytes then zeros, the same on every machine ---
    static_assert(jstr::Utf8::cell_units == 4);
    check(sizeof(StringView<>) == 2 * sizeof(std::size_t), "a view is a pointer and a size: cells someone else owns");
    const String<> widths = "aé€\U0001F600"_js;
    check(widths.count_code_points() == 4 && widths.size_utf8() == 1 + 2 + 3 + 4, "one cell each, whatever the length in bytes");
    check(packed(widths) == "a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80", "and packed, the bytes are UTF-8 again");
    std::array<char, 9> room{};
    check(widths.write_utf8(room) == std::nullopt && room == std::array<char, 9>{}, "write_utf8 writes nothing into too little room");
    std::array<char, 12> enough{};
    check(widths.write_utf8(enough) == std::optional<std::size_t>{10} && std::string_view{enough.data(), 10} == packed(widths),
          "and the packed bytes into enough");

    // --- Moving and copying ---
    String<> source = "moved"_js;
    const String<> moved_to{std::move(source)};
    check(moved_to == "moved"_js, "moving keeps the text");
    check(source.empty() && source.count_code_points() == 0, "a String moved from is empty");
    String<> assigned = "before"_js;
    String<> giver = "after"_js;
    assigned = std::move(giver);
    check(assigned == "after"_js && giver.empty(), "move assignment: the giver is empty");
    String<> self = "self"_js;
    String<>& same = self;
    self = std::move(same);
    check(self == "self"_js, "moving a String onto itself keeps it");
    String<> original = "first"_js;
    const String<> duplicate = original;
    original += "!"_js;
    check(duplicate == "first"_js && original == "first!"_js, "a copy is its own text");

    // Copy assignment between text inside and text on the heap, each way round.
    const String<> long_text = String<>::from(runtime_bytes("this text is longer than sixteen code points")).value();
    String<> was_on_heap = long_text;
    const String<> short_text = "short"_js;
    was_on_heap = short_text;
    check(was_on_heap == "short"_js && wrong_answers(was_on_heap) == 0, "copying short text onto long: the short text");
    was_on_heap += was_on_heap;
    check(was_on_heap == "shortshort"_js, "and it can still grow");
    String<> was_inside = "x"_js;
    was_inside = long_text;
    check(was_inside == long_text && wrong_answers(was_inside) == 0, "copying long text onto short");

    // --- Appending, joining ---
    String<> built;
    built += "one"_js;
    built.append(" two"_js).append(" three"_js);
    check(built == "one two three"_js, "append and += add at the end");
    String<> twice = "ab"_js;
    twice += twice;
    check(twice == "abab"_js, "appending a String to itself doubles it");
    twice.append(twice.view());
    check(twice == "abababab"_js, "and so does appending its own view");
    const String<> a = "a"_js;
    const String<> b = "b"_js;
    check(a + b == "ab"_js && a + "c"_js == "ac"_js && "c"_js + a == "ca"_js && StringView<>{"x"_js} + "y"_js == "xy"_js, "joining in each form");
    check(a == "a"_js && b == "b"_js, "joining changes neither side");
    String<> kept = "kept"_js;
    kept += ""_js;
    check(kept == "kept"_js, "appending nothing changes nothing");

    // --- to_c_string ---
    const auto c = literal.to_c_string();
    check(c.has_value() && std::string_view{c->data()} == "caf\xC3\xA9" && c->size() == 6, "to_c_string ends with a zero after the bytes");
    check(empty.to_c_string().has_value() && empty.to_c_string()->size() == 1, "to_c_string of empty text is an empty C string");
    const auto with_zero = String<>::from(runtime_bytes(std::string_view{"\xC3\xA9\0x", 4}));
    check(with_zero.has_value() && with_zero->count_code_points() == 3 && with_zero->size_utf8() == 4, "a zero is valid text");
    check(with_zero.has_value() && !with_zero->to_c_string().has_value(), "to_c_string refuses text that holds a zero");

    // U+0000's cell is all zeros, as is the padding after é: a search for it matches only a whole cell.
    const StringView<> zero = *with_zero->at(jstr::CodePointIndex{1});
    check(zero == U'\0' && with_zero->contains(zero), "U+0000 is found where it is");
    const auto around_zero = with_zero->split_once(zero);
    check(around_zero.has_value() && around_zero->first == "é"_js && around_zero->second == "x"_js,
          "and only there, not in the zeros after é");
    check(!String<>{"éx"_js}.contains(zero), "nor in text with no U+0000, padding and all");

    // --- Comparing: cells as bytes, in code point order on every machine ---
    check(String<>{"abc"_js} == StringView<>{"abc"_js} && StringView<>{"abc"_js} == String<>{"abc"_js}, "equal both ways");
    check("abc"_js.view() != StringView<>{"abd"_js}, "and unequal");
    check(StringView<>{"a"_js} < StringView<>{"b"_js} && StringView<>{"ab"_js} > StringView<>{"a"_js}, "ordered");
    check(StringView<>{"ÿ"_js} < StringView<>{"Ā"_js} && StringView<>{"￿"_js} < StringView<>{"\U00010000"_js},
          "code point order, across lengths");
    check(StringView<>{"z"_js} < StringView<>{"é"_js} && StringView<>{"\x7F"_js} < StringView<>{"\u0080"_js},
          "a byte above 7F sorts after ASCII, compared unsigned");

    // --- Searching ---
    const StringView<> path = "dir/file.txt"_js;
    check(path.starts_with("dir/"_js) && path.ends_with(".txt"_js) && path.contains("file"_js), "starts, ends, contains");
    check(!path.starts_with("file"_js) && !path.ends_with("dir"_js), "and the misses");
    const auto parts = path.split_once("/"_js);
    check(parts.has_value() && parts->first == "dir"_js && parts->second == "file.txt"_js, "split at the first separator");
    check(!path.split_once("|"_js).has_value(), "nothing when the separator is not there");
    check(!path.split_once(""_js).has_value(), "nothing for an empty separator");
    const auto at_end = StringView<>{"key="_js}.split_once("="_js);
    check(at_end.has_value() && at_end->first == "key"_js && at_end->second.empty(), "a separator at the end leaves an empty after");
    check(StringView<>{"café été"_js}.contains("ét"_js) && !StringView<>{"café"_js}.contains("e"_js),
          "searching mixed text: a whole code point, never part of one");

    // --- One code point, by its index ---
    using jstr::CodePointIndex;
    const String<> data = "This is a test"_js;
    check(data.at(CodePointIndex{6}) == U's' && data.at(CodePointIndex{6}) == "s"_js, "code point 6 (counted from 0), as a char32_t or as text");
    check(data.at(CodePointIndex{7}) == U' ', "and code point 7 is the space");
    check(data.at(CodePointIndex{0}) == U'T' && data.at(CodePointIndex{13}) == U't', "the first and the last");
    check(!data.at(CodePointIndex{14}).has_value() && !data.at(CodePointIndex{99}).has_value(), "nothing past the end");
    check(!empty.at(CodePointIndex{0}).has_value(), "nothing in empty text");
    const String<> accented = "Thé is a test"_js;
    check(accented.at(CodePointIndex{5}) == U's', "an index counts code points, not bytes: past the two-byte é");
    check(accented.at(CodePointIndex{2}) == U'é' && accented.at(CodePointIndex{2})->size_utf8() == 2, "the two-byte code point itself, whole");
    const StringView<> emoji = "a\U0001F600b"_js;
    check(emoji.at(CodePointIndex{1}) == U'\U0001F600' && emoji.at(CodePointIndex{2}) == U'b', "past a four-byte code point");
    check(!(StringView<>{"ab"_js} == U'a') && !(StringView<>{} == U'a'), "== a code point means exactly that one code point");

    // --- Counting from 1, and pieces ---
    using jstr::CodePointCount;
    using jstr::CodePointNumber;
    using jstr::CodePointPosition;
    using jstr::IndexBase;
    check(data.at(CodePointNumber{7}) == U's' && data.at(CodePointNumber{8}) == U' ', "counted from 1, the 7th is code point 6");
    check(data.at(CodePointPosition<IndexBase::one_based>{1}) == U'T' && data.at(CodePointPosition{0}) == U'T', "the base, spelled out");
    check(!data.at(CodePointNumber{0}).has_value(), "counted from 1, there is no 0th");
    check(!data.at(CodePointNumber{15}).has_value() && data.at(CodePointNumber{14}) == U't', "and the 14th is the last");
    check(data.substring(CodePointIndex{0}, CodePointCount{4}) == "This"_js, "four from the start, counted from 0");
    check(data.substring(CodePointNumber{1}, CodePointCount{4}) == "This"_js, "the same four, counted from 1");
    check(data.substring(CodePointNumber{11}) == "test"_js && data.substring(CodePointIndex{10}) == "test"_js, "to the end");
    check(data.substring(CodePointIndex{14}) == ""_js && data.substring(CodePointIndex{14}, CodePointCount{0}) == ""_js,
          "just past the end is the empty text there");
    check(!data.substring(CodePointIndex{15}).has_value(), "further is nothing");
    check(!data.substring(CodePointIndex{11}, CodePointCount{4}).has_value(), "a piece running past the end is nothing, not a shorter piece");
    check(accented.substring(CodePointNumber{3}, CodePointCount{1}) == "é"_js, "a two-byte code point, whole");
    check(accented.substring(CodePointIndex{2}, CodePointCount{3}) == "é i"_js, "a piece that starts with one");

    const auto replaced = data.replace(CodePointNumber{1}, CodePointCount{4}, "That"_js);
    check(replaced.has_value() && *replaced == "That is a test"_js, "replace makes a new String");
    check(data == "This is a test"_js, "and leaves the old one as it was");
    check(data.replace(CodePointIndex{10}, CodePointCount{4}, "été"_js) == "This is a été"_js, "with longer, wider code points");
    check(data.replace(CodePointIndex{4}, CodePointCount{0}, "!"_js) == "This! is a test"_js, "a count of 0 inserts");
    check(data.replace(CodePointIndex{14}, CodePointCount{0}, "s"_js) == "This is a tests"_js, "at the end, appends");
    check(data.replace(CodePointIndex{0}, CodePointCount{5}, ""_js) == "is a test"_js, "with nothing, removes");
    check(!data.replace(CodePointIndex{12}, CodePointCount{3}, "x"_js).has_value(), "nothing when the piece runs past the end");
    check(!data.replace(CodePointNumber{0}, CodePointCount{1}, "x"_js).has_value(), "nothing for a 0th, counted from 1");
    check(accented.replace(CodePointNumber{3}, CodePointCount{1}, "e"_js) == "The is a test"_js, "replacing a two-byte code point whole");
    check(StringView<>{"abc"_js}.replace(CodePointNumber{2}, CodePointCount{1}, "é"_js) == "aéc"_js, "and from a view");

    // --- Where the text is kept: inside the String or on the heap. Each way gives the same answers at every position.
    using Tiny = jstr::String<jstr::Utf8, 4>;
    using HeapOnly = jstr::String<jstr::Utf8, 0>;
    const std::string mixed_bytes = runtime_bytes("aé日\U0001F600 the café naïve über fox \U0001F600\U0001F600 end. "
                                                  "and more ASCII to pass sixteen units at a step, then 日本語.");
    const std::string ascii_bytes(200, 'q');
    const auto mixed = String<>::from(mixed_bytes);
    check(mixed.has_value() && packed(*mixed) == mixed_bytes && wrong_answers(*mixed) == 0, "mixed text on the heap: every position");
    check(mixed.has_value() && wrong_answers(mixed->view()) == 0, "and a view of it");
    check(wrong_answers(HeapOnly::from(mixed_bytes).value()) == 0, "every text on the heap");
    check(wrong_answers(String<>::from(ascii_bytes).value()) == 0, "ASCII on the heap");
    check(wrong_answers(String<>{"aé日\U0001F600 inside"_js}) == 0, "short mixed text, inside the String");
    check(wrong_answers(String<>{""_js}) == 0 && wrong_answers(HeapOnly{""_js}) == 0, "empty text, either way");

    // Text that comes in: a wide code point first, one after 100 ASCII, mostly three-byte code points, long ASCII, the
    // same through from_lossy, and bad text after a long valid run. Spread 16 ASCII bytes at a step, and one at a time
    // around the rest.
    std::string wide_first = runtime_bytes("é");
    const std::string ascii_run(100, 'a');
    std::string cjk;
    for (int i = 0; i < 40; ++i) cjk += runtime_bytes("日本語");
    cjk += "x";
    for (int i = 0; i < 70; ++i) wide_first += "b";
    const std::string late_wide = ascii_run + runtime_bytes("\U0001F600") + ascii_run;
    check(packed(String<>::from(wide_first).value()) == wide_first && wrong_answers(String<>::from(wide_first).value()) == 0,
          "in: a wide code point first");
    check(packed(String<>::from(late_wide).value()) == late_wide && wrong_answers(String<>::from(late_wide).value()) == 0,
          "in: 100 ASCII before the first wide one");
    check(packed(String<>::from(cjk).value()) == cjk && wrong_answers(String<>::from(cjk).value()) == 0, "in: three-byte code points");
    check(wrong_answers(String<>::from(ascii_run).value()) == 0, "in: long ASCII");
    check(packed(String<>::from_lossy(late_wide)) == late_wide, "from_lossy of valid text is the text");
    const auto bad_late = String<>::from(late_wide + runtime_bytes("\xFF"));
    check(!bad_late.has_value() && bad_late.error() == EncodingError{late_wide.size()}, "bad text after a long valid run");
    check(packed(String<>::from_lossy(late_wide + runtime_bytes("\xFF"))) == late_wide + runtime_bytes("�"),
          "and lossy, a U+FFFD where it was");

    // Appending moves text from inside to the heap, and keeps every answer right.
    Tiny grown;
    String<> grown_ascii;
    std::size_t growing_wrong = 0;
    for (const StringView<> piece : {StringView<>{"ab"_js}, StringView<>{"c"_js}, StringView<>{"é"_js}, StringView<>{"\U0001F600xyz"_js},
                                     StringView<>{"0123456789abcdef0123"_js}, StringView<>{"日"_js}}) {
        grown += piece;
        growing_wrong += wrong_answers(grown);
    }
    for (int i = 0; i < 40; ++i) {
        grown_ascii += "abc"_js;
        growing_wrong += wrong_answers(grown_ascii);
    }
    grown_ascii += "é"_js;
    growing_wrong += wrong_answers(grown_ascii);
    check(growing_wrong == 0, "append: inside to heap, at every step");
    check(grown == "abcé\U0001F600xyz0123456789abcdef0123日"_js && grown.count_code_points() == 29, "and the text and count are right");
    Tiny doubled{"éa"_js};
    doubled += doubled;
    doubled += doubled;
    check(doubled == "éaéaéaéa"_js && wrong_answers(doubled) == 0, "appending to itself, across the move to the heap");
    String<> self_mixed = String<>::from(mixed_bytes).value();
    self_mixed += self_mixed;
    check(packed(self_mixed) == mixed_bytes + mixed_bytes && wrong_answers(self_mixed) == 0, "appending to itself, on the heap");
    String<> with_room;
    with_room.reserve_code_points(100);
    with_room += "0123456789"_js;
    with_room += with_room;
    with_room += with_room;
    check(with_room.count_code_points() == 40 && wrong_answers(with_room) == 0, "appending to itself where there is room already");

    // Moving: from inside and from the heap, the source is left empty.
    Tiny inside_source{"ab"_js};
    const Tiny inside_moved{std::move(inside_source)};
    check(inside_moved == "ab"_js && inside_source.empty() && inside_source.count_code_points() == 0, "moving short text");
    check(inside_source.to_c_string().has_value() && inside_source.to_c_string()->size() == 1, "leaves an empty C string");
    String<> heap_source = String<>::from(mixed_bytes).value();
    String<> heap_target = "x"_js;
    heap_target = std::move(heap_source);
    check(packed(heap_target) == mixed_bytes && wrong_answers(heap_target) == 0, "moving text on the heap");
    check(heap_source.empty() && heap_source.count_code_points() == 0 && wrong_answers(heap_source) == 0, "and leaves the source empty");
    heap_source += "é"_js;
    check(heap_source == "é"_js && wrong_answers(heap_source) == 0, "a String moved from can be used again");

    // Room made ahead, and replace and join through it.
    String<> reserved;
    reserved.reserve_code_points(500);
    reserved += "é"_js;
    check(reserved == "é"_js && wrong_answers(reserved) == 0, "reserve_code_points puts even short text on the heap, and it answers");
    const auto wide = mixed->replace(CodePointIndex{3}, CodePointCount{2}, "ééé"_js);
    check(wide.has_value() && wrong_answers(*wide) == 0 && wide->count_code_points() == mixed->count_code_points() + 1, "replace keeps the count");
    const String<> joined_mixed = *mixed + *mixed;
    check(packed(joined_mixed) == mixed_bytes + mixed_bytes && wrong_answers(joined_mixed) == 0, "joining");
    check(*mixed == String<>::from(mixed_bytes).value() && !(*mixed == joined_mixed) && *mixed < joined_mixed, "String with String");

    // --- FixedString: all inside, no destructor, all zeros is empty ---
    using Payload = FixedString<8>;
    static_assert(std::is_trivially_copyable_v<Payload> && std::is_trivially_destructible_v<Payload>);
    static_assert(sizeof(Payload) == 8 * 4 + sizeof(std::size_t));
    const auto from_zeros = std::bit_cast<Payload>(std::array<std::byte, sizeof(Payload)>{});
    check(from_zeros.empty() && from_zeros == ""_js, "all zeros is the empty text");
    Payload payload;
    check(payload.assign_lossy(runtime_bytes("caf\xC3\xA9")) == 4 && payload == "café"_js, "assign_lossy keeps valid text");
    check(payload.assign_lossy(runtime_bytes("0123456789")) == 8 && payload == "01234567"_js, "and cuts it after 8 code points");
    check(payload.assign_lossy(runtime_bytes("\xC3\xA9\xFF\U0001F600")) == 3 && payload == "é�\U0001F600"_js,
          "a bad byte made U+FFFD, written over longer text with nothing cleared");
    check(payload.assign_lossy(runtime_bytes("日日日日日日日日日")) == 8 && payload.view().size_utf8() == 24,
          "eight three-byte code points fill all 8 cells");
    check(wrong_answers(payload.view()) == 0, "and answer at every position");
    check(Payload::from("short"_js).has_value() && !Payload::from("nine code"_js).has_value(), "from(): whole, or nothing when it does not fit");
    payload.clear();
    check(payload.empty() && payload == ""_js, "clear() empties it");
    std::ostringstream fixed_out;
    fixed_out << Payload::from_lossy(runtime_bytes("ab")) << std::format("{:>4}", Payload::from_lossy(runtime_bytes("c")));
    check(fixed_out.str() == "ab   c", "printed and formatted packed");

    // --- Code points ---
    check(code_points_of("aé€\U0001F600"_js) == std::vector<char32_t>{U'a', U'é', U'€', U'\U0001F600'},
          "code points of each length, in order");
    check(code_points_of(""_js).empty(), "empty text has none");

    // --- Printing and hashing ---
    std::ostringstream out;
    out << literal << '|' << view;
    check(out.str() == "caf\xC3\xA9|na\xC3\xAFve", "printed to a stream as its bytes");
    std::ostringstream long_out;
    long_out << *mixed << *mixed << *mixed;
    check(long_out.str() == mixed_bytes + mixed_bytes + mixed_bytes, "text longer than the piece printed at a time");
    check(std::format("[{}] [{:>6}]", literal, StringView<>{"ab"_js}) == "[caf\xC3\xA9] [    ab]", "std::format, with options");
    std::unordered_set<String<>> seen;
    seen.insert("x"_js);
    seen.insert(String<>{"x"_js});
    seen.insert("y"_js);
    check(seen.size() == 2 && seen.contains("x"_js), "hashes alike for equal text");
    return check.finish();
}
