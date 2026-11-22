// File: tests/compile_fail/mixed_types.cpp
// Purpose: Text of two Types never meets without being converted by name: comparing them does not compile.
#include "jString/v001/jstring.hpp"

using namespace jstr::literals;

int main() {
    const jstr::String<jstr::Type::UTF16> wide = "text"_js;
    const jstr::StringView<jstr::Type::UTF8Full> cells = "text"_js;
#ifdef JSTR_BREAK
    const bool same = wide == cells;
#else
    const bool same = wide == jstr::convert<jstr::Type::UTF16>(cells);
#endif
    static_cast<void>(same);
}
