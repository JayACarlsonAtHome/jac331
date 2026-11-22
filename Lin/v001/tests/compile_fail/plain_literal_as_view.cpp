// File: tests/compile_fail/plain_literal_as_view.cpp
// Purpose: A plain "text" is packed bytes with no cells, so it is not text: a view takes "text"_js, whose cells the
//          compiler makes and checks.
#include "jString/v001/jstring.hpp"

using namespace jstr::literals;

int main() {
#ifdef JSTR_BREAK
    const jstr::StringView<jstr::Type::UTF8Full> view = "text";
#else
    const jstr::StringView<jstr::Type::UTF8Full> view = "text"_js;
#endif
    static_cast<void>(view);
}
