// File: tests/compile_fail/compare_with_number.cpp
// Purpose: Text compares with a code point only as a char32_t (U's'), never with a number, which could be anything.
#include "jString/v001/jstring.hpp"

using namespace jString::literals;

int main() {
    const jString::StringView<jString::Type::UTF8Full> letter = "s"_js;
#ifdef JSTR_BREAK
    const bool same = letter == 115;
#else
    const bool same = letter == U's';
#endif
    static_cast<void>(same);
}
