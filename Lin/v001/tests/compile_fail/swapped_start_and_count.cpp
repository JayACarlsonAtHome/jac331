// File: tests/compile_fail/swapped_start_and_count.cpp
// Purpose: A start and a count are different types: swapping them must not compile.
#include "jString/v001/jstring.hpp"

using namespace jString::literals;

int main() {
    const jString::String<jString::Type::UTF8Full> text = "This is a test"_js;
#ifdef JSTR_BREAK
    const auto piece = text.substring(jString::CodePointCount{4}, jString::CodePointIndex{0});
    static_cast<void>(piece);
#else
    const auto piece = text.substring(jString::CodePointIndex{0}, jString::CodePointCount{4});
    static_cast<void>(piece);
#endif
    static_cast<void>(text);
}
