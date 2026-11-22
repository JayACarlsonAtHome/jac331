// File: tests/compile_fail/swapped_start_and_count.cpp
// Purpose: A start and a count are different types: swapping them must not compile.
#include "jString/v001/jstring.hpp"

using namespace jstr::literals;

int main() {
    const jstr::String<jstr::Type::UTF8Full> text = "This is a test"_js;
#ifdef JSTR_BREAK
    const auto piece = text.substring(jstr::CodePointCount{4}, jstr::CodePointIndex{0});
    static_cast<void>(piece);
#else
    const auto piece = text.substring(jstr::CodePointIndex{0}, jstr::CodePointCount{4});
    static_cast<void>(piece);
#endif
    static_cast<void>(text);
}
