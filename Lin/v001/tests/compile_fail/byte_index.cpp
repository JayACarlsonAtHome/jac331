// File: tests/compile_fail/byte_index.cpp
// Purpose: Text has no operator[]: a byte offset can land inside a code point. code_points() walks whole ones.
#include "jString/v001/jstring.hpp"

using namespace jstr::literals;

int main() {
    const jstr::String<> text = "\xC3\xA9t\xC3\xA9"_js;
#ifdef JSTR_BREAK
    const auto first = text[0];
#else
    const auto first = *text.code_points().begin();
#endif
    static_cast<void>(first);
}
