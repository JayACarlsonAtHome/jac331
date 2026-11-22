// File: tests/compile_fail/index_base_compared_with_int.cpp
// Purpose: IndexBase is a type of its own: comparing it with a number must not compile.
#include "jString/v001/jstring.hpp"

using namespace jstr::literals;

// The base is chosen at run time, from argc, which no compiler can know.
int main(int argc, char**) {
    const jstr::String<jstr::Type::UTF8Full> text = "This is a test"_js;
#ifdef JSTR_BREAK
    const bool same = jstr::IndexBase::one_based == 1;
    static_cast<void>(same);
    static_cast<void>(argc);
#else
    const jstr::IndexBase base = argc > 1 ? jstr::IndexBase::zero_based : jstr::IndexBase::one_based;
    const bool same = base == jstr::IndexBase::one_based;
    static_cast<void>(same);
#endif
    static_cast<void>(text);
}
