// File: tests/compile_fail/substring_of_temporary.cpp
// Purpose: A piece of a String that is about to be destroyed would point at freed memory: it must not compile.
#include "jString/v001/jstring.hpp"

using namespace jstr::literals;

int main() {
    const jstr::String<jstr::Type::UTF8Full> text = "This is a test"_js;
#ifdef JSTR_BREAK
    const auto piece = jstr::String<jstr::Type::UTF8Full>{text}.substring(jstr::CodePointIndex{0});
    static_cast<void>(piece);
#else
    const auto piece = text.substring(jstr::CodePointIndex{0});
    static_cast<void>(piece);
#endif
    static_cast<void>(text);
}
