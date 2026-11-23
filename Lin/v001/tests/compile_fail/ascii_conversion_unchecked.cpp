// File: tests/compile_fail/ascii_conversion_unchecked.cpp
// Purpose: Converting to Ascii can fail, so it gives a std::expected, never a String that quietly lost text.
#include "jString/v001/jstring.hpp"

using namespace jString::literals;

int main(int count, char**) {
    const jString::String<jString::Type::UTF8Packed> text = count > 1 ? jString::String<jString::Type::UTF8Packed>{"caf\xC3\xA9"_js} : jString::String<jString::Type::UTF8Packed>{"cafe"_js};
#ifdef JSTR_BREAK
    const jString::String<jString::Type::Ascii> plain = jString::convert<jString::Type::Ascii>(text);
#else
    const jString::String<jString::Type::Ascii> plain = jString::convert<jString::Type::Ascii>(text).value_or(jString::String<jString::Type::Ascii>{});
#endif
    static_cast<void>(plain);
}
