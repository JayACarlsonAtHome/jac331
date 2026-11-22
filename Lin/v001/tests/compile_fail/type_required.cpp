// File: tests/compile_fail/type_required.cpp
// Purpose: The Type has no default: every String names how its text is kept.
#include "jString/v001/jstring.hpp"

int main() {
#ifdef JSTR_BREAK
    const jstr::String<> text;
#else
    const jstr::String<jstr::Type::UTF8Full> text;
#endif
    static_cast<void>(text);
}
