// File: tests/compile_fail/view_to_string_implicitly.cpp
// Purpose: A view must not be copied into a String without saying so: every copy is named.
#include "jString/v001/jstring.hpp"

using namespace jstr::literals;

namespace {

[[nodiscard]] jstr::String<jstr::Type::UTF8Full> keep(jstr::StringView<jstr::Type::UTF8Full> view) {
#ifdef JSTR_BREAK
    return view;
#else
    return jstr::String<jstr::Type::UTF8Full>{view};
#endif
}

}  // namespace

int main() { static_cast<void>(keep("text"_js)); }
