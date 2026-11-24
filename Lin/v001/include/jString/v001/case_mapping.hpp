// File: include/jString/v001/case_mapping.hpp
// Purpose: The case changes behind String's to_lower(), to_upper(), to_title(), to_headline() and
//          capitalize_sentences(), worked on valid kept text of any Type. Written 2026-11-22 by Claude Opus 5.5.
// Modified 2026-11-24 by Claude Opus 5.5: every Type, not UTF-8 only; the comparisons, the mapping and the change are
//          template arguments, so no switch is left.
//
//          The mappings are Unicode's full case mappings (case_tables.hpp), the same in every language: a code point
//          can become up to three (ß to SS, ŉ to ʼN, ﬁ to Fi), so the text can grow. Capital sigma lowers to final
//          sigma (ς) at the end of a word and to σ elsewhere: Final_Sigma, as the Unicode Standard (3.13) defines it.
//          The language-specific rules (Turkish and Azeri dotted i, Lithuanian accents) are not used. Ascii text holds
//          only U+0000 to U+007F, where every mapping is A-Z to a-z and back, one to one: it never grows.
//
//          Words, for to_title() and to_headline(): a word is a run of letters, marks, numbers, connector
//          punctuation and other cased characters (such as circled letters), and a Case_Ignorable character (an
//          apostrophe, a full stop) between two of them does not end it, so "don't" is one word. A word's first code
//          point is title-cased and the rest lowered: "21st century" -> "21st Century", "ǆemal" -> "ǅemal".
//
//          to_headline() is English headline style: as to_title(), but the small words (a, an, the, and, but, or, nor,
//          for, so, yet, as, at, by, in, of, off, on, per, to, up, via, vs, v) are lowered, except the first word, the
//          last word, and a word after a colon.
//
//          capitalize_sentences() raises only the first letter of each sentence and leaves every other code point as
//          it is. A sentence starts the text, and starts after a Sentence_Terminal character (. ! ? and their kind in
//          every script) followed by any closing punctuation and then white space. Opening punctuation and white space
//          before its first letter are passed over. An abbreviation ("e.g. this") is taken for a sentence's end.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "case_tables.hpp"
#include "type.hpp"

namespace jString::inline v001::detail {

enum class CaseChange : unsigned char { lower, upper, title, headline, sentences };

namespace casing {

using case_tables::CaseMap;
using case_tables::CodePointRange;
using case_tables::FullCaseMap;

// cp is one of these code points.
template <char32_t... Wanted>
[[nodiscard]] constexpr bool one_of(char32_t cp) noexcept {
    return ((cp == Wanted) || ...);
}

// cp is from First to Last, both included. A range written backwards does not compile.
template <char32_t First, char32_t Last>
    requires(First <= Last)
[[nodiscard]] constexpr bool between(char32_t cp) noexcept {
    return cp >= First && cp <= Last;
}

template <std::size_t N>
[[nodiscard]] constexpr bool in(const std::array<CodePointRange, N>& table, char32_t cp) noexcept {
    // The number of ranges that start at or before cp; the last of them is the only one that can hold it.
    const auto before = static_cast<std::size_t>(std::ranges::upper_bound(table, cp, {}, &CodePointRange::first) - table.begin());
    return before > 0 && cp <= table.at(before - 1).last;
}

[[nodiscard]] constexpr bool is_ascii_letter(char32_t cp) noexcept { return between<U'A', U'Z'>(cp) || between<U'a', U'z'>(cp); }

// ASCII is answered without a table: most text is mostly ASCII. (Each answer is what the table gives: test_case.cpp
// checks every one.)
[[nodiscard]] constexpr bool is_cased(char32_t cp) noexcept { return cp < 0x80 ? is_ascii_letter(cp) : in(case_tables::cased, cp); }
[[nodiscard]] constexpr bool is_case_ignorable(char32_t cp) noexcept {
    return cp < 0x80 ? one_of<U'\'', U'.', U':', U'^', U'`'>(cp) : in(case_tables::case_ignorable, cp);
}
[[nodiscard]] constexpr bool is_word(char32_t cp) noexcept {
    if (cp < 0x80) return is_ascii_letter(cp) || between<U'0', U'9'>(cp) || cp == U'_';
    return in(case_tables::word, cp) || in(case_tables::cased, cp);  // cased symbols too, such as circled letters
}
[[nodiscard]] constexpr bool is_white_space(char32_t cp) noexcept {
    return cp < 0x80 ? (cp == U' ' || between<U'\t', U'\r'>(cp)) : in(case_tables::white_space, cp);
}
[[nodiscard]] constexpr bool is_sentence_terminal(char32_t cp) noexcept {
    return cp < 0x80 ? one_of<U'.', U'!', U'?'>(cp) : in(case_tables::sentence_terminal, cp);
}
[[nodiscard]] constexpr bool is_sentence_opening(char32_t cp) noexcept {
    return cp < 0x80 ? one_of<U'(', U'[', U'{', U'"', U'\''>(cp) : in(case_tables::sentence_opening, cp);
}
[[nodiscard]] constexpr bool is_sentence_closing(char32_t cp) noexcept {
    return cp < 0x80 ? one_of<U')', U']', U'}', U'"', U'\''>(cp) : in(case_tables::sentence_closing, cp);
}

enum class Mapping : unsigned char { lower, upper, title };

// Each mapping's tables, and its bit in case_tables::mapped_blocks.
template <Mapping M>
struct Tables;
template <>
struct Tables<Mapping::lower> {
    static constexpr const auto& full = case_tables::full_lower;
    static constexpr const auto& simple = case_tables::simple_lower;
    static constexpr std::uint8_t block_bit = 1U;
};
template <>
struct Tables<Mapping::upper> {
    static constexpr const auto& full = case_tables::full_upper;
    static constexpr const auto& simple = case_tables::simple_upper;
    static constexpr std::uint8_t block_bit = 2U;
};
template <>
struct Tables<Mapping::title> {
    static constexpr const auto& full = case_tables::full_title;
    static constexpr const auto& simple = case_tables::simple_title;
    static constexpr std::uint8_t block_bit = 4U;
};

// An ASCII code point mapped: lower moves A-Z to a-z, upper and title move a-z to A-Z, and nothing else changes.
template <Mapping M>
[[nodiscard]] constexpr char32_t ascii_mapped(char32_t cp) noexcept {
    constexpr char32_t step = U'a' - U'A';
    if constexpr (M == Mapping::lower) {
        return between<U'A', U'Z'>(cp) ? cp + step : cp;
    } else {
        return between<U'a', U'z'>(cp) ? cp - step : cp;
    }
}

// The new text, kept as T keeps it, built one code point at a time. A class that owns its vector, not a vector
// passed by reference: clang++ 23 reports a reference parameter that is appended to twice as invalidated
// (compiler_reports 1), and the way round it is to keep the vector where only members change it.
template <Type T>
class Builder {
public:
    constexpr explicit Builder(std::size_t room) : units_(room) {}

    constexpr void add(char32_t cp) {
        make_room();
        // A code point below U+0080 is one unit of that value in every Type but UTF8Full (a whole cell): most text is
        // mostly these.
        if constexpr (T != Type::UTF8Full) {
            if (cp < 0x80) {
                put(static_cast<unit_of<T>>(cp));
                return;
            }
        }
        const Encoded<T> made = encode_kept<T>(cp);
        for (const auto unit : made.view()) put(unit);
    }

    // cp mapped by M: its full mapping (up to three code points) when it has one, else its simple one, else itself.
    template <Mapping M>
    constexpr void add_mapped(char32_t cp) {
        if (cp < 0x80) {  // ASCII: no table
            add(ascii_mapped<M>(cp));
            return;
        }
        // Most scripts have no case: one look at the block map passes them over.
        const std::size_t block = static_cast<std::size_t>(cp) >> 8U;
        if (block >= case_tables::mapped_blocks.size() || (case_tables::mapped_blocks.at(block) & Tables<M>::block_bit) == 0) {
            add(cp);
            return;
        }
        const auto& full = Tables<M>::full;
        const auto f = std::ranges::lower_bound(full, cp, {}, &FullCaseMap::from);
        if (f != full.end() && f->from == cp) {
            for (std::size_t i = 0; i < f->length; ++i) add(f->to.at(i));
            return;
        }
        const auto& simple = Tables<M>::simple;
        const auto s = std::ranges::lower_bound(simple, cp, {}, &CaseMap::from);
        add((s != simple.end() && s->from == cp) ? s->to : cp);
    }

    // Lower or upper of text that is all ASCII: unit by unit, no tables, no decoding. A code point below U+0080 is
    // one unit of that value in every Type, and in UTF8Full the rest of its cell is zeros, which map to themselves.
    // All Ascii text is this. Gives false, with nothing added, when the text is not all ASCII.
    template <Mapping M>
    [[nodiscard]] constexpr bool add_ascii_only(kept<T> text) {
        if (!std::ranges::all_of(text, [](auto unit) noexcept { return value_of(unit) < 0x80U; })) return false;
        if (units_.size() - used_ < text.size()) units_.resize(used_ + text.size());
        // One pass over the text, which compilers make wide.
        std::ranges::transform(text, std::span{units_}.subspan(used_).begin(),
                               [](auto unit) noexcept { return static_cast<unit_of<T>>(ascii_mapped<M>(static_cast<char32_t>(value_of(unit)))); });
        used_ += text.size();
        return true;
    }

    // Each code point mapped by M on its own, with nothing to see on either side.
    template <Mapping M>
    constexpr void add_each_mapped(kept<T> text) {
        for (std::size_t at = 0; at < text.size(); at += length_at<T>(text, at)) {
            // A unit below 80 is a whole ASCII code point in every Type but UTF8Full: mapped here, with nothing decoded.
            if constexpr (T != Type::UTF8Full) {
                if (value_of(text[at]) < 0x80U) {
                    make_room();
                    put(static_cast<unit_of<T>>(ascii_mapped<M>(static_cast<char32_t>(value_of(text[at])))));
                    continue;
                }
            }
            add_mapped<M>(decode_at<T>(text, at));
        }
    }

    [[nodiscard]] constexpr std::vector<unit_of<T>> take() && {
        units_.resize(used_);
        return std::move(units_);
    }

private:
    // The units are written by index into room made ahead, not by push_back: g++ stops inlining in a large file, and
    // then each push_back was a call, which made a case change of mixed text 3 to 4 times slower in some Types.
    // Room for one code point's units: a code point is at most max_kept<T> units.
    constexpr void make_room() {
        if (units_.size() - used_ < max_kept<T>) units_.resize(std::max(2 * units_.size(), used_ + max_kept<T>));
    }
    constexpr void put(unit_of<T> unit) noexcept {
        units_[used_] = unit;
        ++used_;
    }

    std::vector<unit_of<T>> units_;  // room, of which the first used_ units are the text
    std::size_t used_{};
};

// Final_Sigma (Unicode 3.13): the sigma at at follows a cased letter (with any case-ignorable characters between),
// and no cased letter follows it (with any case-ignorable characters between).
[[nodiscard]] constexpr bool final_sigma(std::span<const char32_t> text, std::size_t at) noexcept {
    bool cased_before = false;
    for (std::size_t i = at; i > 0; --i) {
        if (is_case_ignorable(text[i - 1])) continue;
        cased_before = is_cased(text[i - 1]);
        break;
    }
    if (!cased_before) return false;
    for (std::size_t i = at + 1; i < text.size(); ++i) {
        if (is_case_ignorable(text[i])) continue;
        return !is_cased(text[i]);
    }
    return true;
}

inline constexpr char32_t capital_sigma = U'Σ';

template <Type T>
constexpr void add_lower(Builder<T>& out, std::span<const char32_t> text, std::size_t at) {
    if (text[at] == capital_sigma) {  // final sigma, or sigma
        out.add(final_sigma(text, at) ? U'\u03C2' : U'\u03C3');
        return;
    }
    out.template add_mapped<Mapping::lower>(text[at]);
}

// Whether each code point is part of a word: a word character, or a case-ignorable one between two word characters.
[[nodiscard]] constexpr std::vector<bool> word_marks(std::span<const char32_t> text) {
    std::vector<bool> marks(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        marks[i] = is_word(text[i]) ||
                   (is_case_ignorable(text[i]) && i > 0 && is_word(text[i - 1]) && i + 1 < text.size() && is_word(text[i + 1]));
    }
    return marks;
}

struct Word {
    std::size_t first;
    std::size_t end;
};

[[nodiscard]] constexpr std::vector<Word> words_of(const std::vector<bool>& marks) {
    std::vector<Word> words;
    for (std::size_t i = 0; i < marks.size(); ++i) {
        if (!marks[i]) continue;
        if (i == 0 || !marks[i - 1]) {
            words.push_back(Word{i, i + 1});
        } else {
            words.back().end = i + 1;
        }
    }
    return words;
}

// English headline style's small words, lower case.
inline constexpr std::array<std::u32string_view, 23> small_words{
    U"a", U"an", U"the", U"and", U"but", U"or", U"nor", U"for", U"so", U"yet", U"as", U"at",
    U"by", U"in", U"of", U"off", U"on", U"per", U"to", U"up", U"via", U"vs", U"v"};

[[nodiscard]] constexpr bool is_small_word(std::span<const char32_t> word) noexcept {
    std::array<char32_t, 3> lowered{};
    if (word.size() > lowered.size()) return false;
    for (std::size_t i = 0; i < word.size(); ++i) {
        if (word[i] >= 0x80) return false;  // the small words are ASCII
        lowered.at(i) = ascii_mapped<Mapping::lower>(word[i]);
    }
    return std::ranges::find(small_words, std::u32string_view{lowered.data(), word.size()}) != small_words.end();
}

[[nodiscard]] constexpr bool after_colon(std::span<const char32_t> text, std::size_t first) noexcept {
    for (std::size_t i = first; i > 0; --i) {
        if (is_white_space(text[i - 1])) continue;
        return text[i - 1] == U':';
    }
    return false;
}

template <Type T, bool Headline>
constexpr void title_words(Builder<T>& out, std::span<const char32_t> text) {
    const std::vector<Word> words = words_of(word_marks(text));
    std::size_t next_word = 0;
    std::size_t i = 0;
    while (i < text.size()) {
        if (next_word < words.size() && words[next_word].first == i) {
            const Word word = words[next_word];
            bool keep_small = false;
            if constexpr (Headline) {
                keep_small = next_word != 0 && next_word + 1 != words.size() && !after_colon(text, word.first) &&
                             is_small_word(text.subspan(word.first, word.end - word.first));
            }
            if (keep_small) {
                add_lower<T>(out, text, i);
            } else {
                out.template add_mapped<Mapping::title>(text[i]);
            }
            for (std::size_t j = word.first + 1; j < word.end; ++j) add_lower<T>(out, text, j);
            i = word.end;
            ++next_word;
            continue;
        }
        out.add(text[i]);
        ++i;
    }
}

template <Type T>
constexpr void capitalize_sentence_starts(Builder<T>& out, std::span<const char32_t> text) {
    bool at_start = true;     // looking for a sentence's first letter
    bool after_stop = false;  // a sentence terminal has been seen, perhaps followed by closing punctuation
    for (const char32_t cp : text) {
        if (at_start) {
            if (is_white_space(cp) || is_sentence_opening(cp)) {
                out.add(cp);
                continue;
            }
            at_start = false;
            if (is_cased(cp)) {
                out.template add_mapped<Mapping::title>(cp);
                continue;
            }
        }
        out.add(cp);
        if (is_sentence_terminal(cp)) {
            after_stop = true;
        } else if (after_stop && is_white_space(cp)) {
            after_stop = false;
            at_start = true;
        } else if (after_stop && !is_sentence_closing(cp)) {
            after_stop = false;
        }
    }
}

template <Type T>
[[nodiscard]] constexpr std::vector<char32_t> decoded(kept<T> text) {
    std::vector<char32_t> all;
    all.reserve(count_of<T>(text));
    for (std::size_t at = 0; at < text.size(); at += length_at<T>(text, at)) all.push_back(decode_at<T>(text, at));
    return all;
}

}  // namespace casing

// valid kept text with change C made, as new kept units of the same Type.
template <Type T, CaseChange C>
[[nodiscard]] constexpr std::vector<unit_of<T>> change_case(kept<T> text) {
    using casing::Mapping;
    casing::Builder<T> out{text.size() + text.size() / 8 + 4};

    if constexpr (C == CaseChange::upper) {
        if (!out.template add_ascii_only<Mapping::upper>(text)) out.template add_each_mapped<Mapping::upper>(text);
    } else if constexpr (C == CaseChange::lower) {
        if (!out.template add_ascii_only<Mapping::lower>(text)) {
            // A capital sigma needs to see its neighbours; text with none is mapped one code point at a time.
            if (!find<T>(text, encode_kept<T>(casing::capital_sigma).view()).has_value()) {
                out.template add_each_mapped<Mapping::lower>(text);
            } else {
                const std::vector<char32_t> all = casing::decoded<T>(text);
                for (std::size_t i = 0; i < all.size(); ++i) casing::add_lower<T>(out, all, i);
            }
        }
    } else if constexpr (C == CaseChange::title || C == CaseChange::headline) {
        casing::title_words<T, C == CaseChange::headline>(out, casing::decoded<T>(text));
    } else {
        static_assert(C == CaseChange::sentences);
        casing::capitalize_sentence_starts<T>(out, casing::decoded<T>(text));
    }
    return std::move(out).take();
}

}  // namespace jString::inline v001::detail
