// File: tests/compile_fail/runtime_array_as_literal.cpp
// Purpose: An array filled while the program runs is not a literal, and must not pass for one: from() is the way.
#include <string_view>

#include "jString/v001/jstring.hpp"

int main(int count, char**) {
    char filled[3]{static_cast<char>('a' + count), 'b', '\0'};
#ifdef JSTR_BREAK
    const jString::StringView<jString::Type::UTF8Full> view = filled;
#else
    const auto view = jString::String<jString::Type::UTF8Full>::from(std::string_view{filled});
#endif
    static_cast<void>(view);
}
