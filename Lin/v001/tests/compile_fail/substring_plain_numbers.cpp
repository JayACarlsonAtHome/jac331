// File: tests/compile_fail/substring_plain_numbers.cpp
// Purpose: substring takes a typed start and count, never two plain numbers: substring(7, 3) says nothing about which is which, or how they count.
#include "jString/v001/jstring.hpp"

using namespace jString::literals;

int main() {
    const jString::String<jString::Type::UTF8Full> text = "This is a test"_js;
#ifdef JSTR_BREAK
    const auto piece = text.substring(7, 3);
    static_cast<void>(piece);
#else
    const auto piece = text.substring(jString::CodePointNumber{7}, jString::CodePointCount{3});
    static_cast<void>(piece);
#endif
    static_cast<void>(text);
}
