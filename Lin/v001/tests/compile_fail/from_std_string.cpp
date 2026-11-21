// File: tests/compile_fail/from_std_string.cpp
// Purpose: A std::string must not become a String without a check: from() is the way.
#include <string>

#include "jString/v001/jstring.hpp"

int main(int count, char**) {
    const std::string raw(static_cast<std::size_t>(count), 'a');
#ifdef JSTR_BREAK
    const jstr::String<> text = raw;
#else
    const auto text = jstr::String<>::from(raw);
#endif
    static_cast<void>(text);
}
