#!/usr/bin/env python3
# File: tools/gen_case_tables.py
# Purpose: Writes include/jString/v001/case_tables.hpp from the Unicode Character Database: the case mappings and the
#          character properties that jString's to_lower, to_upper, to_title, to_headline and capitalize_sentences use.
#          Written 2026-11-22 by Claude Opus 5.5.
#
#   python3 -I tools/gen_case_tables.py <ucd-dir> [<output>]
#
# <ucd-dir> holds UnicodeData.txt, SpecialCasing.txt, DerivedCoreProperties.txt and PropList.txt of one Unicode version
# (https://www.unicode.org/Public/<version>/ucd/). The header records that version and each file's SHA-256, so the
# tables can be traced to their source and rebuilt for the next version.
#
# What is taken:
#   - simple (one code point to one) lower, upper and title mappings from UnicodeData.txt; an empty title mapping
#     means the upper one, as the Unicode Standard says
#   - the unconditional full mappings (one code point to up to three) from SpecialCasing.txt. The conditional ones are
#     left out: Final_Sigma is coded in case_mapping.hpp, and the language-specific ones (Turkish, Azeri, Lithuanian)
#     are not wanted: jString's case mapping is the same in every language
#   - Cased and Case_Ignorable (DerivedCoreProperties.txt), Sentence_Terminal and White_Space (PropList.txt)
#   - general categories, as ranges: word characters (letters, marks, numbers, connector punctuation), and the
#     punctuation that can open (Ps, Pi) or close (Pe, Pf) a sentence
import hashlib
import os
import sys

FILES = ["UnicodeData.txt", "SpecialCasing.txt", "DerivedCoreProperties.txt", "PropList.txt"]


def fields(path):
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if line:
                yield [part.strip() for part in line.split(";")]


def version_of(path):
    with open(path, encoding="utf-8") as f:
        first = f.readline()
    # "# SpecialCasing-17.0.0.txt"
    name = first.strip().lstrip("#").strip()
    return name.rsplit("-", 1)[1].removesuffix(".txt")


def ranges_of(code_points):
    out = []
    for cp in sorted(code_points):
        if out and out[-1][1] + 1 == cp:
            out[-1][1] = cp
        else:
            out.append([cp, cp])
    return out


def main():
    if len(sys.argv) not in (2, 3):
        print(__doc__ or "usage: gen_case_tables.py <ucd-dir> [<output>]")
        sys.exit(2)
    ucd = sys.argv[1]
    here = os.path.dirname(os.path.abspath(__file__))
    out_path = sys.argv[2] if len(sys.argv) == 3 else os.path.join(here, "..", "include", "jString", "v001", "case_tables.hpp")

    versions = {version_of(os.path.join(ucd, f)) for f in FILES if f != "UnicodeData.txt"}
    if len(versions) != 1:
        sys.exit(f"the files are from different Unicode versions: {sorted(versions)}")
    version = versions.pop()
    digests = {f: hashlib.sha256(open(os.path.join(ucd, f), "rb").read()).hexdigest() for f in FILES}

    # --- UnicodeData.txt: categories (with First/Last ranges) and simple mappings
    category = {}
    lower, upper, title = {}, {}, {}
    pending_first = None
    for row in fields(os.path.join(ucd, "UnicodeData.txt")):
        cp = int(row[0], 16)
        name, cat = row[1], row[2]
        if name.endswith(", First>"):
            pending_first = cp
            continue
        if name.endswith(", Last>"):
            for c in range(pending_first, cp + 1):
                category[c] = cat
            pending_first = None
            continue
        category[cp] = cat
        up = int(row[12], 16) if row[12] else None
        lo = int(row[13], 16) if row[13] else None
        ti = int(row[14], 16) if row[14] else up
        if up is not None and up != cp:
            upper[cp] = up
        if lo is not None and lo != cp:
            lower[cp] = lo
        if ti is not None and ti != cp:
            title[cp] = ti

    # --- SpecialCasing.txt: unconditional full mappings only
    special = {"lower": {}, "title": {}, "upper": {}}
    for row in fields(os.path.join(ucd, "SpecialCasing.txt")):
        conditions = row[4] if len(row) > 4 else ""
        if conditions:
            continue
        cp = int(row[0], 16)
        for key, column in (("lower", 1), ("title", 2), ("upper", 3)):
            mapped = [int(x, 16) for x in row[column].split()]
            if mapped != [cp]:
                special[key][cp] = mapped
    for key, table in special.items():
        for cp, mapped in table.items():
            if len(mapped) > 3:
                sys.exit(f"{key} mapping of U+{cp:04X} is longer than 3 code points")

    # --- properties
    def property_set(file, wanted):
        out = set()
        for row in fields(os.path.join(ucd, file)):
            if row[1] != wanted:
                continue
            if ".." in row[0]:
                a, b = row[0].split("..")
                out.update(range(int(a, 16), int(b, 16) + 1))
            else:
                out.add(int(row[0], 16))
        return out

    cased = property_set("DerivedCoreProperties.txt", "Cased")
    case_ignorable = property_set("DerivedCoreProperties.txt", "Case_Ignorable")
    sentence_terminal = property_set("PropList.txt", "Sentence_Terminal")
    white_space = property_set("PropList.txt", "White_Space")
    word = {cp for cp, cat in category.items() if cat[0] in "LMN" or cat == "Pc"}
    # The inverted marks that open a Spanish question or exclamation (and the inverted interrobang) are Po, not Ps.
    opening = {cp for cp, cat in category.items() if cat in ("Ps", "Pi")} | {0x22, 0x27, 0xA1, 0xBF, 0x2E18}
    closing = {cp for cp, cat in category.items() if cat in ("Pe", "Pf")} | {0x22, 0x27}

    # --- write
    def pairs(name, table):
        items = sorted(table.items())
        body = ",".join(f"{{0x{a:X},0x{b:X}}}" for a, b in items)
        return f"inline constexpr std::array<CaseMap, {len(items)}> {name}{{{{{wrap(body)}}}}};\n"

    def specials(name, table):
        items = sorted(table.items())
        body = ",".join("{0x%X,{%s},%d}" % (a, ",".join(f"0x{x:X}" for x in (b + [0] * (3 - len(b)))), len(b)) for a, b in items)
        return f"inline constexpr std::array<FullCaseMap, {len(items)}> {name}{{{{{wrap(body)}}}}};\n"

    def ranges(name, code_points):
        items = ranges_of(code_points)
        body = ",".join(f"{{0x{a:X},0x{b:X}}}" for a, b in items)
        return f"inline constexpr std::array<CodePointRange, {len(items)}> {name}{{{{{wrap(body)}}}}};\n"

    def wrap(body, width=118):
        parts, line, out = body.split("},{"), "", []
        for i, p in enumerate(parts):
            piece = ("{" if i else "") + p + ("}" if i < len(parts) - 1 else "")
            piece += "," if i < len(parts) - 1 else ""
            if len(line) + len(piece) > width:
                out.append(line)
                line = ""
            line += piece
        out.append(line)
        return "\n    " + "\n    ".join(out) + "\n"

    # Which 256-code-point blocks hold any mapping (bit 1 lower, 2 upper, 4 title): most scripts have no case, so most
    # code points are passed over with one look here instead of two searches.
    keys = {1: set(lower) | set(special["lower"]), 2: set(upper) | set(special["upper"]), 4: set(title) | set(special["title"])}
    last_block = max(max(k) for k in keys.values()) >> 8
    blocks = [sum(bit for bit, ks in keys.items() if any((b << 8) <= k < ((b + 1) << 8) for k in ks)) for b in range(last_block + 1)]
    block_text = wrap(",".join(str(b) for b in blocks).replace(",", "},{"))
    block_text = block_text.replace("{", "").replace("}", "")

    sources = "\n".join(f"//            {f:<27} sha256 {digests[f]}" for f in FILES)
    text = f"""// File: include/jString/v001/case_tables.hpp
// Purpose: Unicode {version} case mappings and character properties, for case_mapping.hpp. Generated by
//          tools/gen_case_tables.py from the Unicode Character Database: do not edit; run the script again for a new
//          Unicode version.
//          Sources (https://www.unicode.org/Public/{version}/ucd/):
{sources}
//          Every table is sorted by code point, so it is searched by halves.
#pragma once

#include <array>
#include <cstdint>

namespace jString::inline v001::detail::case_tables {{

inline constexpr char unicode_version[] = "{version}";

struct CaseMap {{
    char32_t from;
    char32_t to;
}};

struct FullCaseMap {{
    char32_t from;
    std::array<char32_t, 3> to;
    std::uint8_t length;
}};

struct CodePointRange {{
    char32_t first;
    char32_t last;
}};

// One code point to one (UnicodeData.txt). A code point not listed maps to itself.
{pairs("simple_lower", lower)}{pairs("simple_upper", upper)}{pairs("simple_title", title)}
// One code point to up to three, whatever the language (SpecialCasing.txt, its unconditional rows). These come first.
{specials("full_lower", special["lower"])}{specials("full_upper", special["upper"])}{specials("full_title", special["title"])}
// Bit 1: the block of 256 code points holds a lower mapping; 2: an upper; 4: a title. Blocks past the end hold none.
inline constexpr std::array<std::uint8_t, {len(blocks)}> mapped_blocks{{{{{block_text}}}}};

// Properties.
{ranges("cased", cased)}{ranges("case_ignorable", case_ignorable)}{ranges("sentence_terminal", sentence_terminal)}{ranges("white_space", white_space)}
// Letters, marks, numbers and connector punctuation: what a word is made of.
{ranges("word", word)}
// What may stand before the first letter of a sentence (Ps, Pi, the straight quotes and the inverted ¡ ¿ ⸘) or after
// its last stop (Pe, Pf, and the straight quotes).
{ranges("sentence_opening", opening)}{ranges("sentence_closing", closing)}
}}  // namespace jString::inline v001::detail::case_tables
"""
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(text)
    print(f"wrote {out_path}: Unicode {version}, lower {len(lower)}+{len(special['lower'])}, upper {len(upper)}+{len(special['upper'])}, "
          f"title {len(title)}+{len(special['title'])}, {os.path.getsize(out_path)} bytes")


if __name__ == "__main__":
    main()
