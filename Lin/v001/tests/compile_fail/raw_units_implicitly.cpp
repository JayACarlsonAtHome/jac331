// File: tests/compile_fail/raw_units_implicitly.cpp
// Purpose: Text must not turn back into unchecked bytes without saying so: units() is the way, by name.
#include <string_view>

#include "jString/v001/jstring.hpp"

using namespace jstr::literals;

int main() {
    const jstr::String<> text = "text"_js;
#ifdef JSTR_BREAK
    const std::string_view bytes = text;
#else
    const std::string_view bytes = text.units();
#endif
    static_cast<void>(bytes);
}
