// File: tests/compile_fail/view_to_string_implicitly.cpp
// Purpose: A view must not be copied into a String without saying so: every copy is named.
#include "jString/v001/jstring.hpp"

using namespace jString::literals;

namespace {

[[nodiscard]] jString::String<jString::Type::UTF8Full> keep(jString::StringView<jString::Type::UTF8Full> view) {
#ifdef JSTR_BREAK
    return view;
#else
    return jString::String<jString::Type::UTF8Full>{view};
#endif
}

}  // namespace

int main() { static_cast<void>(keep("text"_js)); }
