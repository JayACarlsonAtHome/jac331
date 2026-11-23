// File: tests/compile_fail/index_base_from_int.cpp
// Purpose: IndexBase is a type of its own: a number is not one.
#include "jString/v001/jstring.hpp"

using namespace jString::literals;

int main() {
    const jString::String<jString::Type::UTF8Full> text = "This is a test"_js;
#ifdef JSTR_BREAK
    const jString::IndexBase base = 1;
    static_cast<void>(base);
#else
    const jString::IndexBase base = jString::IndexBase::one_based;
    static_cast<void>(base);
#endif
    static_cast<void>(text);
}
