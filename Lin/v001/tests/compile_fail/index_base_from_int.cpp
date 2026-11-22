// File: tests/compile_fail/index_base_from_int.cpp
// Purpose: IndexBase is a type of its own: a number is not one.
#include "jString/v001/jstring.hpp"

using namespace jstr::literals;

int main() {
    const jstr::String<jstr::Type::UTF8Full> text = "This is a test"_js;
#ifdef JSTR_BREAK
    const jstr::IndexBase base = 1;
    static_cast<void>(base);
#else
    const jstr::IndexBase base = jstr::IndexBase::one_based;
    static_cast<void>(base);
#endif
    static_cast<void>(text);
}
