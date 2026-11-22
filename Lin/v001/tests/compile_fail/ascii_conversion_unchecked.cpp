// File: tests/compile_fail/ascii_conversion_unchecked.cpp
// Purpose: Converting to Ascii can fail, so it gives a std::expected, never a String that quietly lost text.
#include "jString/v001/jstring.hpp"

using namespace jstr::literals;

int main(int count, char**) {
    const jstr::String<jstr::Type::UTF8Packed> text = count > 1 ? jstr::String<jstr::Type::UTF8Packed>{"caf\xC3\xA9"_js} : jstr::String<jstr::Type::UTF8Packed>{"cafe"_js};
#ifdef JSTR_BREAK
    const jstr::String<jstr::Type::Ascii> plain = jstr::convert<jstr::Type::Ascii>(text);
#else
    const jstr::String<jstr::Type::Ascii> plain = jstr::convert<jstr::Type::Ascii>(text).value_or(jstr::String<jstr::Type::Ascii>{});
#endif
    static_cast<void>(plain);
}
