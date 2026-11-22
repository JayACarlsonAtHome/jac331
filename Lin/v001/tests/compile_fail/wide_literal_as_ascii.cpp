// File: tests/compile_fail/wide_literal_as_ascii.cpp
// Purpose: A literal is Ascii text only when it is ASCII: "café"_js is not, and is refused while compiling.
#include "jString/v001/jstring.hpp"

using namespace jstr::literals;

int main() {
#ifdef JSTR_BREAK
    const jstr::String<jstr::Type::Ascii> text = "caf\xC3\xA9"_js;
#else
    const jstr::String<jstr::Type::Ascii> text = "cafe"_js;
#endif
    static_cast<void>(text);
}
