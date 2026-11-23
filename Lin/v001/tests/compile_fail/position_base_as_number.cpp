// File: tests/compile_fail/position_base_as_number.cpp
// Purpose: The base of a position is an IndexBase, never a number: CodePointPosition<1> must not compile.
#include "jString/v001/jstring.hpp"

using namespace jString::literals;

int main() {
    const jString::String<jString::Type::UTF8Full> text = "This is a test"_js;
#ifdef JSTR_BREAK
    const jString::CodePointPosition<1> where{7};
    static_cast<void>(where);
#else
    const jString::CodePointPosition<jString::IndexBase::one_based> where{7};
    static_cast<void>(where);
#endif
    static_cast<void>(text);
}
