// File: tests/test_error_in_expected.cpp
// Purpose: jCppGUI's DataGrid::tallest_line_in_use, with jString in place of std::string: an error holding text,
//          returned inside a std::expected beside a reference, its text moved into a member. With std::string, g++ 15
//          at -O2 (the gcc-warn preset) reported "dangling pointer to 'face' may be used" there. A String never
//          points into itself, so this must build clean with every compiler. The run checks the values too.
#include <algorithm>
#include <expected>
#include <functional>
#include <optional>
#include <utility>

#include "check.hpp"
#include "jString/v001/jstring.hpp"

namespace {

using namespace jstr::literals;

struct Error {
    int code;
    jstr::String<> message;
};

class Engine {
public:
    explicit Engine(bool has_face) : has_face_{has_face} {}
    [[nodiscard]] std::expected<std::reference_wrapper<const Engine>, Error> monospace_face() const {
        if (has_face_) return std::cref(*this);
        return std::unexpected(Error{7, "this shaper has no monospace face"_js});
    }
    [[nodiscard]] int line_height() const { return has_face_ ? 18 : 16; }

private:
    bool has_face_;
};

class Grid {
public:
    [[nodiscard]] int tallest_line_in_use(const Engine& text) {
        auto face = text.monospace_face();
        if (face) return std::max(text.line_height(), face->get().line_height());
        refuse(std::move(face.error()));
        return text.line_height();
    }
    [[nodiscard]] std::optional<Error> take_refusal() { return std::exchange(refusal_, std::nullopt); }

private:
    void refuse(Error why) { refusal_ = std::move(why); }
    std::optional<Error> refusal_{};
};

}  // namespace

int main() {
    jstr_test::Checker check;
    Grid grid;
    check(grid.tallest_line_in_use(Engine{true}) == 18, "with a face, its height");
    check(!grid.take_refusal().has_value(), "and no refusal");
    check(grid.tallest_line_in_use(Engine{false}) == 16, "without one, the regular height");
    const auto refusal = grid.take_refusal();
    check(refusal.has_value() && refusal->code == 7 && refusal->message == "this shaper has no monospace face"_js, "and the refusal, intact");
    return check.finish();
}
