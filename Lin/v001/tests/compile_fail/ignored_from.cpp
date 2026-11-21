// File: tests/compile_fail/ignored_from.cpp
// Purpose: from()'s answer must not be thrown away: it is the only place that says the bytes were bad.
#include <string_view>

#include "jString/v001/jstring.hpp"

int main(int count, char**) {
    const std::string_view raw{count > 1 ? "two" : "one"};
#ifdef JSTR_BREAK
    jstr::String<>::from(raw);
#else
    const auto text = jstr::String<>::from(raw);
    static_cast<void>(text);
#endif
}
