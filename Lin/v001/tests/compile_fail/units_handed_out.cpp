// File: tests/compile_fail/units_handed_out.cpp
// Purpose: A view never hands out its cells as units: there is no units(). Text goes out packed, by name.
#include <vector>

#include "jString/v001/jstring.hpp"

using namespace jstr::literals;

int main() {
    const jstr::StringView<> text = "caf\xC3\xA9"_js;
#ifdef JSTR_BREAK
    const auto bytes = text.units();
#else
    const std::vector<char> bytes = text.to_utf8();
#endif
    static_cast<void>(bytes);
}
