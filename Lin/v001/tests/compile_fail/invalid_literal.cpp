// File: tests/compile_fail/invalid_literal.cpp
// Purpose: A literal that is not valid UTF-8 must not compile, as a view too: here an overlong zero, C0 80.
#include "jString/v001/jstring.hpp"

using namespace jString::literals;

int main() {
#ifdef JSTR_BREAK
    const jString::StringView<jString::Type::UTF8Full> view = "bad \xC0\x80"_js;
#else
    const jString::StringView<jString::Type::UTF8Full> view = "good \xC3\xA9"_js;
#endif
    static_cast<void>(view);
}
