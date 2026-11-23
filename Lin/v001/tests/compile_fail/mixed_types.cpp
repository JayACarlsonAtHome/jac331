// File: tests/compile_fail/mixed_types.cpp
// Purpose: Text of two Types never meets without being converted by name: comparing them does not compile.
#include "jString/v001/jstring.hpp"

using namespace jString::literals;

int main() {
    const jString::String<jString::Type::UTF16> wide = "text"_js;
    const jString::StringView<jString::Type::UTF8Full> cells = "text"_js;
#ifdef JSTR_BREAK
    const bool same = wide == cells;
#else
    const bool same = wide == jString::convert<jString::Type::UTF16>(cells);
#endif
    static_cast<void>(same);
}
