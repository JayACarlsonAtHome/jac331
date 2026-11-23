// File: tests/compile_fail/raw_units_implicitly.cpp
// Purpose: Text must not turn back into bytes without saying so: it goes out packed, by name (to_utf8()), never as a
//          std::string_view.
#include <string_view>
#include <vector>

#include "jString/v001/jstring.hpp"

using namespace jString::literals;

int main() {
    const jString::String<jString::Type::UTF8Full> text = "text"_js;
#ifdef JSTR_BREAK
    const std::string_view bytes = text;
#else
    const std::vector<char> bytes = text.to_utf8();
#endif
    static_cast<void>(bytes);
}
