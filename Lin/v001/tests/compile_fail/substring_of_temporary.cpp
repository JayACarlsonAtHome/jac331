// File: tests/compile_fail/substring_of_temporary.cpp
// Purpose: A piece of a String that is about to be destroyed would point at freed memory: it must not compile.
#include "jString/v001/jstring.hpp"

using namespace jString::literals;

int main() {
    const jString::String<jString::Type::UTF8Full> text = "This is a test"_js;
#ifdef JSTR_BREAK
    const auto piece = jString::String<jString::Type::UTF8Full>{text}.substring(jString::CodePointIndex{0});
    static_cast<void>(piece);
#else
    const auto piece = text.substring(jString::CodePointIndex{0});
    static_cast<void>(piece);
#endif
    static_cast<void>(text);
}
