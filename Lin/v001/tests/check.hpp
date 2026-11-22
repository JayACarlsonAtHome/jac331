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
            std::cerr << "FAILED " << where.file_name() << ':' << where.line() << ": " << context_ << what << '\n';
        }
    }

    // Said before each failure from here on: which of several runs of the same checks failed.
    void context(const char* name) { context_ = name; }

    [[nodiscard]] int finish() const {
        std::cout << checks_ << " checks, " << failures_ << " failures\n";
        return failures_ == 0 ? 0 : 1;
    }

private:
    const char* context_{""};
    int checks_{};
    int failures_{};
};

}  // namespace jstr_test
