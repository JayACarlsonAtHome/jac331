// File: tests/test_string.cpp
// Purpose: String and StringView at run time: every way in, the promises in string.hpp's header (moved-from is
//          empty, appending to itself, c_str and zeros), comparing, searching, joining, printing and hashing.
#include <format>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "check.hpp"
#include "jString/v001/jstring.hpp"

namespace {

using jstr::EncodingError;
using jstr::String;
using jstr::StringView;
using namespace jstr::literals;

// Text built at run time, so the compiler cannot check it: the way bytes from a file or a C library arrive.
[[nodiscard]] std::string runtime_bytes(std::string_view bytes) { return std::string{bytes}; }

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
    check(empty.empty() && empty.size_units() == 0 && empty.units().empty(), "a new String is empty");
    const String<> literal = "café"_js;
    check(literal.units() == "caf\xC3\xA9" && literal.size_units() == 5, "a literal, checked while compiling");
    check(literal.count_code_points() == 4, "four code points in five bytes");
    const StringView<> view = "naïve";
    const String<> copied{view};
    check(copied == view && copied.units().data() != view.units().data(), "a String from a view owns a copy");

    const auto good = String<>::from(runtime_bytes("\xE2\x82\xAC 5"));
    check(good.has_value() && *good == "€ 5", "from() takes valid bytes");
    const auto broken = String<>::from(runtime_bytes("price \xE2\x82 5"));
    check(!broken.has_value() && broken.error() == EncodingError{6}, "from() says where the bytes go wrong");
    const auto view_good = StringView<>::from("plain");
    check(view_good.has_value() && *view_good == "plain", "StringView::from takes valid units");
    check(!StringView<>::from(runtime_bytes("\xFF")).has_value(), "StringView::from refuses bad units");

    // from_lossy: each maximal subpart becomes one U+FFFD.
    check(String<>::from_lossy(runtime_bytes("ok")) == "ok", "lossy keeps valid text as it is");
    check(String<>::from_lossy(runtime_bytes("a\x80" "b")) == "a�b", "a lone continuation byte");
    check(String<>::from_lossy(runtime_bytes("\xF0\x80\x80")) == "���", "F0 80 80: three bad bytes, three U+FFFD");
    check(String<>::from_lossy(runtime_bytes("\xE1\x80" "A")) == "�" "A", "a cut sequence is one U+FFFD");
    check(String<>::from_lossy(runtime_bytes("")).empty(), "lossy of nothing is empty");

    // --- Moving and copying ---
    String<> source = "moved"_js;
    const String<> moved_to{std::move(source)};
    check(moved_to == "moved", "moving keeps the text");
    check(source.empty() && source.units().empty(), "a String moved from is empty");
    String<> assigned = "before"_js;
    String<> giver = "after"_js;
    assigned = std::move(giver);
    check(assigned == "after" && giver.empty(), "move assignment: the giver is empty");
    String<> self = "self"_js;
    String<>& same = self;
    self = std::move(same);
    check(self == "self", "moving a String onto itself keeps it");
    String<> original = "first"_js;
    const String<> duplicate = original;
    original += "!";
    check(duplicate == "first" && original == "first!", "a copy is its own text");

    // --- Appending, joining ---
    String<> built;
    built += "one";
    built.append(" two").append(" three");
    check(built == "one two three", "append and += add at the end");
    String<> twice = "ab"_js;
    twice += twice;
    check(twice == "abab", "appending a String to itself doubles it");
    twice.append(twice.view());
    check(twice == "abababab", "and so does appending its own view");
    const String<> a = "a"_js;
    const String<> b = "b"_js;
    check(a + b == "ab" && a + "c" == "ac" && "c" + a == "ca" && StringView<>{"x"} + "y" == "xy", "joining in each form");
    check(a == "a" && b == "b", "joining changes neither side");
    String<> kept = "kept"_js;
    kept += "";
    check(kept == "kept", "appending nothing changes nothing");

    // --- c_str ---
    const auto c = literal.c_str();
    check(c.has_value() && std::string_view{*c} == "caf\xC3\xA9", "c_str ends with a zero after the text");
    check(empty.c_str().has_value() && std::string_view{*empty.c_str()}.empty(), "c_str of empty text is an empty C string");
    const auto with_zero = String<>::from(runtime_bytes(std::string_view{"a\0b", 3}));
    check(with_zero.has_value() && with_zero->size_units() == 3, "a zero is valid text");
    check(with_zero.has_value() && !with_zero->c_str().has_value(), "c_str refuses text that holds a zero");

    // --- Comparing ---
    check(String<>{"abc"_js} == StringView<>{"abc"} && StringView<>{"abc"} == String<>{"abc"_js}, "equal both ways");
    check("abc"_js.view() != StringView<>{"abd"}, "and unequal");
    check(StringView<>{"a"} < StringView<>{"b"} && StringView<>{"ab"} > StringView<>{"a"}, "ordered");
    check(StringView<>{"ÿ"} < StringView<>{"Ā"} && StringView<>{"￿"} < StringView<>{"\U00010000"},
          "UTF-8 order is code point order, across lengths");

    // --- Searching ---
    const StringView<> path = "dir/file.txt";
    check(path.starts_with("dir/") && path.ends_with(".txt") && path.contains("file"), "starts, ends, contains");
    check(!path.starts_with("file") && !path.ends_with("dir"), "and the misses");
    const auto parts = path.split_once("/");
    check(parts.has_value() && parts->first == "dir" && parts->second == "file.txt", "split at the first separator");
    check(!path.split_once("|").has_value(), "nothing when the separator is not there");
    check(!path.split_once("").has_value(), "nothing for an empty separator");
    const auto at_end = StringView<>{"key="}.split_once("=");
    check(at_end.has_value() && at_end->first == "key" && at_end->second.empty(), "a separator at the end leaves an empty after");

    // --- One code point, by its index ---
    using jstr::CodePointIndex;
    const String<> data = "This is a test"_js;
    check(data.at(CodePointIndex{6}) == U's' && data.at(CodePointIndex{6}) == "s"_js, "code point 6 (counted from 0), as a char32_t or as text");
    check(data.at(CodePointIndex{7}) == U' ', "and code point 7 is the space");
    check(data.at(CodePointIndex{0}) == U'T' && data.at(CodePointIndex{13}) == U't', "the first and the last");
    check(!data.at(CodePointIndex{14}).has_value() && !data.at(CodePointIndex{99}).has_value(), "nothing past the end");
    check(!empty.at(CodePointIndex{0}).has_value(), "nothing in empty text");
    const String<> accented = "Th\u00e9 is a test"_js;
    check(accented.at(CodePointIndex{5}) == U's', "an index counts code points, not bytes: past the two-byte \u00e9");
    check(accented.units().substr(5, 1) == "i", "where byte 5 is something else");
    check(accented.at(CodePointIndex{2}) == U'\u00e9' && accented.at(CodePointIndex{2})->size_units() == 2, "the two-byte code point itself, whole");
    const StringView<> emoji = "a\U0001F600b";
    check(emoji.at(CodePointIndex{1}) == U'\U0001F600' && emoji.at(CodePointIndex{2}) == U'b', "past a four-byte code point");
    check(!(StringView<>{"ab"} == U'a') && !(StringView<>{} == U'a'), "== a code point means exactly that one code point");

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
    check(accented.substring(CodePointNumber{3}, CodePointCount{1}) == "\u00e9"_js, "a two-byte code point, whole");
    check(accented.substring(CodePointIndex{2}, CodePointCount{3}) == "\u00e9 i"_js, "a piece that starts with one");

    const auto replaced = data.replace(CodePointNumber{1}, CodePointCount{4}, "That");
    check(replaced.has_value() && *replaced == "That is a test", "replace makes a new String");
    check(data == "This is a test", "and leaves the old one as it was");
    check(data.replace(CodePointIndex{10}, CodePointCount{4}, "\u00e9t\u00e9") == "This is a \u00e9t\u00e9"_js, "with longer, wider code points");
    check(data.replace(CodePointIndex{4}, CodePointCount{0}, "!") == "This! is a test"_js, "a count of 0 inserts");
    check(data.replace(CodePointIndex{14}, CodePointCount{0}, "s") == "This is a tests"_js, "at the end, appends");
    check(data.replace(CodePointIndex{0}, CodePointCount{5}, "") == "is a test"_js, "with nothing, removes");
    check(!data.replace(CodePointIndex{12}, CodePointCount{3}, "x").has_value(), "nothing when the piece runs past the end");
    check(!data.replace(CodePointNumber{0}, CodePointCount{1}, "x").has_value(), "nothing for a 0th, counted from 1");
    check(accented.replace(CodePointNumber{3}, CodePointCount{1}, "e") == "The is a test"_js, "replacing a two-byte code point whole");

    // --- Code points ---
    check(code_points_of("aé€\U0001F600") == std::vector<char32_t>{U'a', U'é', U'€', U'\U0001F600'},
          "code points of each length, in order");
    check(code_points_of("").empty(), "empty text has none");

    // --- Printing and hashing ---
    std::ostringstream out;
    out << literal << '|' << view;
    check(out.str() == "caf\xC3\xA9|na\xC3\xAFve", "printed to a stream as its units");
    check(std::format("[{}] [{:>6}]", literal, StringView<>{"ab"}) == "[caf\xC3\xA9] [    ab]", "std::format, with options");
    std::unordered_set<String<>> seen;
    seen.insert("x"_js);
    seen.insert(String<>{"x"_js});
    seen.insert("y"_js);
    check(seen.size() == 2 && seen.contains("x"_js), "hashes alike for equal text");
    return check.finish();
}
