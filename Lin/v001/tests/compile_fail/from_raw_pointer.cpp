// File: tests/compile_fail/from_raw_pointer.cpp
// Purpose: A String must not be made from a char pointer: nothing would check the bytes. from() is the way.
#include <string_view>

#include "jString/v001/jstring.hpp"

int main(int count, char**) {
    const char* raw = count > 1 ? "two" : "one";
#ifdef JSTR_BREAK
    const jstr::String<jstr::Type::UTF8Full> text{raw};
#else
    const auto text = jstr::String<jstr::Type::UTF8Full>::from(std::string_view{raw});
#endif
    static_cast<void>(text);
}
