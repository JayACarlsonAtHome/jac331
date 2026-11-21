// File: tests/compile_fail/invalid_literal.cpp
// Purpose: A literal that is not valid UTF-8 must not compile: here an overlong zero, C0 80.
#include "jString/v001/jstring.hpp"

int main() {
#ifdef JSTR_BREAK
    const jstr::StringView<> view = "bad \xC0\x80";
#else
    const jstr::StringView<> view = "good \xC3\xA9";
#endif
    static_cast<void>(view);
}
