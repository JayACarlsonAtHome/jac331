// File: tests/check.hpp
// Purpose: The small test helper: counts checks, prints failures with their line, gives the exit code.
#pragma once

#include <iostream>
#include <source_location>

namespace jstr_test {

class Checker {
public:
    void operator()(bool condition, const char* what, std::source_location where = std::source_location::current()) {
        ++checks_;
        if (!condition) {
            ++failures_;
            std::cerr << "FAILED " << where.file_name() << ':' << where.line() << ": " << what << '\n';
        }
    }

    [[nodiscard]] int finish() const {
        std::cout << checks_ << " checks, " << failures_ << " failures\n";
        return failures_ == 0 ? 0 : 1;
    }

private:
    int checks_{};
    int failures_{};
};

}  // namespace jstr_test
