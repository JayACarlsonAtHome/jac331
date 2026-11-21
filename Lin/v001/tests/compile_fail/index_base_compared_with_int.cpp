// File: tests/compile_fail/index_base_compared_with_int.cpp
// Purpose: IndexBase is a type of its own: comparing it with a number must not compile.
#include "jString/v001/jstring.hpp"

using namespace jstr::literals;

int main() {
    const jstr::String<> text = "This is a test"_js;
#ifdef JSTR_BREAK
    const bool same = jstr::IndexBase::one_based == 1;
    static_cast<void>(same);
#else
    const jstr::IndexBase base = text.empty() ? jstr::IndexBase::zero_based : jstr::IndexBase::one_based;
    const bool same = base == jstr::IndexBase::one_based;
    static_cast<void>(same);
#endif
    static_cast<void>(text);
}
