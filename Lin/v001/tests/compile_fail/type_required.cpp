// File: tests/compile_fail/type_required.cpp
// Purpose: The Type has no default: every String names how its text is kept.
#include "jString/v001/jstring.hpp"

int main() {
#ifdef JSTR_BREAK
    const jString::String<> text;
#else
    const jString::String<jString::Type::UTF8Full> text;
#endif
    static_cast<void>(text);
}
