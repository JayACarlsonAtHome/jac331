// File: tests/compile_fail/std_view_of_text.cpp
// Purpose: A std::string_view cannot be made of jString's text, even by asking: in UTF8Full its units are 4-byte
//          cells, which a reader of packed bytes would misread. The text goes out by name, as UTF-8.
#include <string_view>
#include <vector>

#include "jString/v001/jstring.hpp"

using namespace jstr::literals;

int main() {
    const jstr::String<jstr::Type::UTF8Full> text = "text"_js;
#ifdef JSTR_BREAK
    const std::string_view bytes{text};
    static_cast<void>(bytes);
#else
    const std::vector<char> utf8 = text.to_utf8();
    const std::string_view bytes{utf8.data(), utf8.size()};
    static_cast<void>(bytes);
#endif
}
