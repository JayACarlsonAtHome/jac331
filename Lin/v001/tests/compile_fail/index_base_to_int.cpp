// File: tests/compile_fail/index_base_to_int.cpp
// Purpose: IndexBase is a type of its own: it is not a number either, and never becomes one without being asked.
#include "jString/v001/jstring.hpp"

using namespace jstr::literals;

int main() {
    const jstr::String<> text = "This is a test"_js;
#ifdef JSTR_BREAK
    const int number = jstr::IndexBase::one_based;
    static_cast<void>(number);
#else
    const auto number = static_cast<int>(jstr::IndexBase::one_based);
    static_cast<void>(number);
#endif
    static_cast<void>(text);
}
