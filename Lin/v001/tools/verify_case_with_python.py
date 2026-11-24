#!/usr/bin/env python3
# File: tools/verify_case_with_python.py
# Purpose: Checks jString's to_lower() and to_upper() against Python's str.lower() and str.upper() for every Unicode
#          scalar value, and for many generated strings that test Final_Sigma. Python is an independent implementation
#          of the same Unicode full case mappings, so where both use the same Unicode version they must agree exactly.
#          Also compares to_title() with str.title() for every single code point (one code point is one word).
#          Written 2026-11-22 by Claude Opus 5.5.
# Modified 2026-11-24 by Claude Opus 5.5: every Type. Each line is made in UTF8Packed and converted to the other four;
#          each Type's answers, read back as UTF-8, must all be Python's. Ascii answers for ASCII lines only.
#
#   python3 -I tools/verify_case_with_python.py [g++|clang++]
#
# It builds a small program against include/ in a temporary folder, feeds it the text, and compares. Exit code 0 when
# everything agrees. Run it after regenerating case_tables.hpp for a new Unicode version.
import os
import random
import subprocess
import sys
import tempfile
import unicodedata

TYPES = ["Ascii", "UTF8Packed", "UTF8Full", "UTF16", "UTF32"]
HERE = os.path.dirname(os.path.abspath(__file__))
INCLUDE = os.path.join(HERE, "..", "include")
TABLE_VERSION = open(os.path.join(INCLUDE, "jString", "v001", "case_tables.hpp"), encoding="utf-8").read().split(
    'unicode_version[] = "', 1)[1].split('"', 1)[0]

PROGRAM = r'''
#include <iostream>
#include <string>
#include <vector>
#include "jString/v001/jstring.hpp"
using jString::Type;
template <Type T, class Text>
void answer(const Text& text) {
    const auto out = [](const auto& made) {
        const std::vector<char> utf8 = made.to_utf8();
        std::cout.write(utf8.data(), static_cast<std::streamsize>(utf8.size())) << '\n';
    };
    out(text.to_lower());
    out(text.to_upper());
    out(text.to_title());
}
// One line in, fifteen out: lower, upper and title in each of the five Types (Ascii: "-" three times when the line
// is not ASCII).
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        const auto text = jString::String<Type::UTF8Packed>::from(line);
        if (!text) return 2;
        if (const auto ascii = jString::convert<Type::Ascii>(*text)) {
            answer<Type::Ascii>(*ascii);
        } else {
            std::cout << "-\n-\n-\n";
        }
        answer<Type::UTF8Packed>(*text);
        answer<Type::UTF8Full>(jString::convert<Type::UTF8Full>(*text));
        answer<Type::UTF16>(jString::convert<Type::UTF16>(*text));
        answer<Type::UTF32>(jString::convert<Type::UTF32>(*text));
    }
}
'''


def main():
    compiler = sys.argv[1] if len(sys.argv) > 1 else "g++"
    if TABLE_VERSION != unicodedata.unidata_version:
        sys.exit(f"case_tables.hpp is Unicode {TABLE_VERSION}, this Python is {unicodedata.unidata_version}: use a Python with the same version")

    lines = []
    singles = [c for c in range(0x10FFFF + 1) if not 0xD800 <= c <= 0xDFFF and c not in (0x0A, 0x0D, 0x85, 0x2028, 0x2029)]
    lines += [chr(c) for c in singles]
    # Final_Sigma: capital sigma among cased, uncased and case-ignorable characters.
    pieces = ["Σ", "Α", "α", "ς", "'", ".", "́", " ", "1", "-", "­", "Ⓐ", "ß", "İ", "ǅ"]
    rnd = random.Random(331)
    generated = ["".join(rnd.choice(pieces) for _ in range(rnd.randint(1, 8))) for _ in range(20000)]
    lines += generated

    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "case_probe.cpp")
        exe = os.path.join(tmp, "case_probe")
        open(src, "w").write(PROGRAM)
        # g++ is given the one flag CMakeLists.txt gives it for jString's headers: clang::lifetimebound is clang's.
        flags = [] if "clang" in os.path.basename(compiler) else ["-Wno-attributes=clang::lifetimebound"]
        subprocess.run([compiler, "-std=c++23", "-O2", *flags, "-I", INCLUDE, src, "-o", exe], check=True)
        data = "\n".join(lines) + "\n"
        out = subprocess.run([exe], input=data.encode("utf-8"), capture_output=True, check=True).stdout.decode("utf-8")
    got = out.split("\n")[:-1]
    per_line = 3 * len(TYPES)
    if len(got) != per_line * len(lines):
        sys.exit(f"expected {per_line * len(lines)} lines, got {len(got)}")

    failures = {f"{kind} {name}": [] for name in TYPES for kind in ("lower", "upper", "title")}
    for i, text in enumerate(lines):
        for t, name in enumerate(TYPES):
            if name == "Ascii" and not text.isascii():
                continue
            at = per_line * i + 3 * t
            lower, upper, title = got[at], got[at + 1], got[at + 2]
            if lower != text.lower(): failures[f"lower {name}"].append((text, lower, text.lower()))
            if upper != text.upper(): failures[f"upper {name}"].append((text, upper, text.upper()))
            if i < len(singles) and title != text.title(): failures[f"title {name}"].append((text, title, text.title()))

    print(f"Unicode {TABLE_VERSION}, {compiler}: {len(singles)} code points and {len(generated)} generated strings")
    bad = 0
    for kind, items in failures.items():
        print(f"  {kind}: {len(items)} differences")
        for text, mine, python in items[:8]:
            print(f"    {[hex(ord(c)) for c in text]}: jString {[hex(ord(c)) for c in mine]} python {[hex(ord(c)) for c in python]}")
        bad += len(items)
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
