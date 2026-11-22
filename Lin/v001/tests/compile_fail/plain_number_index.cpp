// File: tests/compile_fail/plain_number_index.cpp
// Purpose: at() takes a CodePointIndex, never a plain number: a number does not say whether it counts bytes or code
//          points, and in UTF-8 the two differ as soon as the text holds a letter like é.
#include "jString/v001/jstring.hpp"

using namespace jstr::literals;

int main() {
    const jstr::String<jstr::Type::UTF8Full> text = "Th\xC3\xA9 is a test"_js;
#ifdef JSTR_BREAK
    const auto found = text.at(5);
#else
    const auto found = text.at(jstr::CodePointIndex{5});
#endif
    static_cast<void>(found);
}
