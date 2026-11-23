// File: tests/compile_fail/invalid_literal_suffix.cpp
// Purpose: A _js literal that is not valid UTF-8 must not compile: here a lone surrogate, ED A0 80.
#include "jString/v001/jstring.hpp"

using namespace jString::literals;

int main() {
#ifdef JSTR_BREAK
    const jString::String<jString::Type::UTF8Full> text = "bad \xED\xA0\x80"_js;
#else
    const jString::String<jString::Type::UTF8Full> text = "good \xED\x9F\xBF"_js;
#endif
    static_cast<void>(text);
}
