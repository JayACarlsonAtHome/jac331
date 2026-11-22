// File: tests/compile_fail/substring_plain_numbers.cpp
// Purpose: substring takes a typed start and count, never two plain numbers: substring(7, 3) says nothing about which is which, or how they count.
#include "jString/v001/jstring.hpp"

using namespace jstr::literals;

int main() {
    const jstr::String<jstr::Type::UTF8Full> text = "This is a test"_js;
#ifdef JSTR_BREAK
    const auto piece = text.substring(7, 3);
    static_cast<void>(piece);
#else
    const auto piece = text.substring(jstr::CodePointNumber{7}, jstr::CodePointCount{3});
    static_cast<void>(piece);
#endif
    static_cast<void>(text);
}
