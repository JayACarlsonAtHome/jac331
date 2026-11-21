// File: tests/compile_fail/view_to_string_implicitly.cpp
// Purpose: A view must not be copied into a String without saying so: every copy is named.
#include "jString/v001/jstring.hpp"

namespace {

[[nodiscard]] jstr::String<> keep(jstr::StringView<> view) {
#ifdef JSTR_BREAK
    return view;
#else
    return jstr::String<>{view};
#endif
}

}  // namespace

int main() { static_cast<void>(keep("text")); }
