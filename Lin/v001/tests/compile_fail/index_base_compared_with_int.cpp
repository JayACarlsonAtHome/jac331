// File: tests/compile_fail/index_base_compared_with_int.cpp
// Purpose: IndexBase is a type of its own: comparing it with a number must not compile.
#include "jString/v001/jstring.hpp"

using namespace jString::literals;

// The base is chosen at run time, from argc, which no compiler can know.
int main(int argc, char**) {
    const jString::String<jString::Type::UTF8Full> text = "This is a test"_js;
#ifdef JSTR_BREAK
    const bool same = jString::IndexBase::one_based == 1;
    static_cast<void>(same);
    static_cast<void>(argc);
#else
    const jString::IndexBase base = argc > 1 ? jString::IndexBase::zero_based : jString::IndexBase::one_based;
    const bool same = base == jString::IndexBase::one_based;
    static_cast<void>(same);
#endif
    static_cast<void>(text);
}
