// File: bench/bench_string.cpp
// Purpose: Times jString against std::string doing the same jobs, and prints one table. jString is timed twice: as it
//          is (String<>, up to 16 code points held inside it) and with every text on the heap (String<Utf8, 0>), so that
//          what the inside buffer buys can be read off. jString keeps a code point to a 4-byte cell; std::string keeps
//          the packed UTF-8, so jString's way out (packing to UTF-8) is timed too. Where std::string has no answer of its own (the 500th code point, a piece
//          counted in code points), it is given the plain correct way: walking the UTF-8, as a programmer would write it.
//          Byte indexing is timed separately and labelled, because it is fast and, for text that is not ASCII, wrong.
//
//          Each job runs in rounds of many repetitions. The time shown is the fastest round's, per repetition: the
//          number least disturbed by whatever else the machine is doing. Every string made has its address and some of
//          its units stored where the compiler must assume they are read, so that no compiler can leave out making it
//          (clang++ did, for std::string, until this was done).
//
//          Build it optimized (gcc-release or clang-release) for numbers worth reading. Any argument (ctest passes
//          --quick) runs every job a few times only, to prove it works.
#include <algorithm>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "jString/v001/jstring.hpp"

namespace {

using jstr::CodePointCount;
using jstr::CodePointIndex;
using jstr::String;
using jstr::StringView;
using jstr::Utf8;
using namespace jstr::literals;

// The same String with every text on the heap: nothing fits inside.
using HeapString = String<Utf8, 0>;

// Each result is stored here, so the compiler cannot decide the work was never needed.
volatile std::size_t g_sink = 0;

void keep(std::size_t value) { g_sink = value; }
// A string made: its address, and its first, middle and last units, so that no compiler can leave out filling it. Its
// address alone was not enough: clang++ still left out copying 1 MiB, since nothing read what was copied.
void keep(std::string_view made) {
    std::size_t seen = std::bit_cast<std::uintptr_t>(made.data());
    if (!made.empty()) {
        for (const char unit : {made.front(), made[made.size() / 2], made.back()}) seen += static_cast<unsigned char>(unit);
    }
    g_sink = seen;
}

// A jString made: its first, middle and last code points, so that no compiler can leave out filling it.
void keep(StringView<> made) {
    std::size_t seen = made.count_code_points();
    if (!made.empty()) {
        for (const std::size_t n : {std::size_t{0}, made.count_code_points() / 2, made.count_code_points() - 1}) {
            for (const char32_t c : made.at(CodePointIndex{n})->code_points()) seen += c;
        }
    }
    g_sink = seen;
}

struct Text {
    std::string bytes;
    std::size_t code_points;
};

// Text of about size bytes, made of the pieces in turn.
[[nodiscard]] Text make_text(std::size_t size, std::span<const std::string_view> pieces) {
    Text text{{}, 0};
    std::size_t next = 0;
    while (text.bytes.size() < size) {
        const std::string_view piece = pieces[next % pieces.size()];
        text.bytes += piece;
        for (const char unit : piece) {
            if ((static_cast<unsigned char>(unit) & 0xC0U) != 0x80U) ++text.code_points;
        }
        ++next;
    }
    return text;
}

// std::string's correct way to the n-th code point: count the bytes that begin one. Gives the byte offset, or the
// size when the text is shorter.
[[nodiscard]] std::size_t std_offset_of(std::string_view bytes, std::size_t n) {
    std::size_t seen = 0;
    for (std::size_t at = 0; at < bytes.size(); ++at) {
        if ((static_cast<unsigned char>(bytes[at]) & 0xC0U) != 0x80U) {
            if (seen == n) return at;
            ++seen;
        }
    }
    return bytes.size();
}

[[nodiscard]] std::size_t std_count(std::string_view bytes) {
    std::size_t count = 0;
    for (const char unit : bytes) {
        if ((static_cast<unsigned char>(unit) & 0xC0U) != 0x80U) ++count;
    }
    return count;
}

// Positions spread over [0, limit), the same every run.
[[nodiscard]] std::vector<std::size_t> positions(std::size_t how_many, std::size_t limit) {
    std::vector<std::size_t> all;
    all.reserve(how_many);
    std::uint64_t state = 0x9E3779B97F4A7C15U;
    for (std::size_t i = 0; i < how_many; ++i) {
        state = state * 6364136223846793005U + 1442695040888963407U;
        all.push_back((state >> 33U) % limit);
    }
    return all;
}

// The texts every job reads, as one kind of jString.
template <class S>
struct Made {
    using string = S;
    S short_ascii;
    S short_mixed;
    S ascii_4k;
    S mixed_4k;
    S mixed_4k_copy;
};

template <class S>
[[nodiscard]] Made<S> made_as(std::string_view short_ascii, std::string_view short_mixed, const Text& ascii_4k, const Text& mixed_4k) {
    return Made<S>{S::from(short_ascii).value(), S::from(short_mixed).value(), S::from(ascii_4k.bytes).value(),
                   S::from(mixed_4k.bytes).value(), S::from(mixed_4k.bytes).value()};
}

template <class M>
using string_of = typename std::remove_cvref_t<M>::string;

class Bench {
public:
    Bench(bool quick, const Made<String<>>& inside, const Made<HeapString>& heap) : quick_{quick}, inside_{inside}, heap_{heap} {}

    // Times one job: std::string's way, then the jString way, given each kind's texts.
    template <class Std, class Jstr>
    void job(std::string_view name, std::size_t reps, const Std& with_std, const Jstr& with_jstr) const {
        const double std_ns = time(reps, with_std);
        const double inside_ns = time(reps, [&] { with_jstr(inside_); });
        const double heap_ns = time(reps, [&] { with_jstr(heap_); });
        std::cout << std::left << std::setw(50) << name << std::right << std::setw(11) << format(std_ns) << std::setw(11) << format(inside_ns)
                  << std::setw(11) << format(heap_ns) << std::setw(9) << ratio(inside_ns, std_ns) << '\n';
    }

    static void heading() {
        std::cout << std::left << std::setw(50) << "job (time for one)" << std::right << std::setw(11) << "std::string" << std::setw(11)
                  << "jString" << std::setw(11) << "heap-only" << std::setw(9) << "jStr/std" << '\n';
        std::cout << std::left << std::setw(50) << "" << std::right << std::setw(11) << "" << std::setw(11) << "(16 inside)" << std::setw(11)
                  << "jString" << '\n';
        std::cout << std::string(92, '-') << '\n';
    }

private:
    template <class Work>
    [[nodiscard]] double time(std::size_t reps, const Work& work) const {
        const std::size_t rounds = quick_ ? 1 : 7;
        const std::size_t count = quick_ ? std::min<std::size_t>(reps, 3) : reps;
        double best = 0;
        for (std::size_t round = 0; round < rounds; ++round) {
            const auto start = std::chrono::steady_clock::now();
            for (std::size_t i = 0; i < count; ++i) work();
            const std::chrono::duration<double, std::nano> took = std::chrono::steady_clock::now() - start;
            const double each = took.count() / static_cast<double>(count);
            if (round == 0 || each < best) best = each;
        }
        return best;
    }

    [[nodiscard]] static std::string format(double ns) {
        std::ostringstream out;
        out << std::fixed;
        if (ns < 1e4) {
            out << std::setprecision(ns < 10 ? 2 : (ns < 100 ? 1 : 0)) << ns << " ns";
        } else if (ns < 1e7) {
            out << std::setprecision(1) << ns / 1e3 << " us";
        } else {
            out << std::setprecision(1) << ns / 1e6 << " ms";
        }
        return out.str();
    }

    [[nodiscard]] static std::string ratio(double a, double b) {
        std::ostringstream out;
        out << std::fixed << std::setprecision(2) << a / b << 'x';
        return out.str();
    }

    bool quick_;
    const Made<String<>>& inside_;
    const Made<HeapString>& heap_;
};

}  // namespace

// Any argument at all is --quick, which ctest passes: reading what it says would mean indexing argv as a C pointer.
int main(int argc, char**) {
    const bool quick = argc > 1;

    const std::vector<std::string_view> ascii_words{"the ", "quick ", "brown ", "fox ", "jumps ", "over ", "lazy ", "dogs. "};
    const std::vector<std::string_view> mixed_words{"the ", "café ", "naïve ", "日本語 ", "fox ", "\U0001F600 ", "über ", "dogs. "};
    const std::string short_ascii = "hello, wor";                       // 10 bytes: fits std::string's own buffer
    const std::string short_mixed = "café naïve 日本";  // 18 bytes, 12 code points: does not
    const std::string medium_ascii(60, 'm');                           // 60 bytes: fits jString's 64, not std::string's 15
    const Text ascii_4k = make_text(4096, ascii_words);
    const Text mixed_4k = make_text(4096, mixed_words);
    const Text mixed_1m = make_text(std::size_t{1} << 20U, mixed_words);

    const Made<String<>> inside = made_as<String<>>(short_ascii, short_mixed, ascii_4k, mixed_4k);
    const Made<HeapString> heap = made_as<HeapString>(short_ascii, short_mixed, ascii_4k, mixed_4k);
    const std::string std_short{short_ascii};
    const std::string std_ascii{ascii_4k.bytes};
    const std::string std_mixed{mixed_4k.bytes};
    const std::string std_mixed_copy{mixed_4k.bytes};
    const std::vector<std::size_t> ascii_at = positions(1024, ascii_4k.code_points);
    const std::vector<std::size_t> mixed_at = positions(1024, mixed_4k.code_points);
    const std::vector<std::size_t> mixed_starts = positions(1024, mixed_4k.code_points - 100);
    const std::vector<std::size_t> short_at = positions(1024, 12);

    std::cout << "jString benchmark" << (quick ? " (--quick: a smoke run, the numbers mean nothing)" : "") << "\n";
    std::cout << "4 KiB ASCII: " << ascii_4k.bytes.size() << " bytes; 4 KiB mixed: " << mixed_4k.bytes.size() << " bytes, "
              << mixed_4k.code_points << " code points\n\n";
    Bench::heading();
    const Bench bench{quick, inside, heap};

    // --- Making text from bytes: std::string copies, jString checks and copies ---
    bench.job("make from 10 bytes ASCII", 2'000'000,
              [&] { const std::string s{short_ascii}; keep(s); },
              [&](const auto& m) { const auto s = string_of<decltype(m)>::from(short_ascii); keep(s->view()); });
    bench.job("make from 18 bytes mixed", 2'000'000,
              [&] { const std::string s{short_mixed}; keep(s); },
              [&](const auto& m) { const auto s = string_of<decltype(m)>::from(short_mixed); keep(s->view()); });
    bench.job("make from 60 bytes ASCII", 2'000'000,
              [&] { const std::string s{medium_ascii}; keep(s); },
              [&](const auto& m) { const auto s = string_of<decltype(m)>::from(medium_ascii); keep(s->view()); });
    bench.job("make from 4 KiB ASCII", 100'000,
              [&] { const std::string s{ascii_4k.bytes}; keep(s); },
              [&](const auto& m) { const auto s = string_of<decltype(m)>::from(ascii_4k.bytes); keep(s->view()); });
    bench.job("make from 4 KiB mixed", 100'000,
              [&] { const std::string s{mixed_4k.bytes}; keep(s); },
              [&](const auto& m) { const auto s = string_of<decltype(m)>::from(mixed_4k.bytes); keep(s->view()); });
    bench.job("make from 1 MiB mixed", 200,
              [&] { const std::string s{mixed_1m.bytes}; keep(s); },
              [&](const auto& m) { const auto s = string_of<decltype(m)>::from(mixed_1m.bytes); keep(s->view()); });
    bench.job("make from 4 KiB mixed, lossy (all valid)", 100'000,
              [&] { const std::string s{mixed_4k.bytes}; keep(s); },
              [&](const auto& m) { const auto s = string_of<decltype(m)>::from_lossy(mixed_4k.bytes); keep(s.view()); });

    // --- Copying ---
    bench.job("copy 10 bytes", 2'000'000,
              [&] { const std::string t{std_short}; keep(t); },
              [&](const auto& m) { const string_of<decltype(m)> t{m.short_ascii}; keep(t.view()); });
    bench.job("copy 4 KiB ASCII", 100'000,
              [&] { const std::string t{std_ascii}; keep(t); },
              [&](const auto& m) { const string_of<decltype(m)> t{m.ascii_4k}; keep(t.view()); });
    bench.job("copy 4 KiB mixed", 100'000,
              [&] { const std::string t{std_mixed}; keep(t); },
              [&](const auto& m) { const string_of<decltype(m)> t{m.mixed_4k}; keep(t.view()); });

    // --- Appending ---
    bench.job("append 10 bytes, 100,000 times (one = all)", 20,
              [&] { std::string s; for (int i = 0; i < 100'000; ++i) s += std_short; keep(s); },
              [&](const auto& m) {
                  string_of<decltype(m)> s;
                  for (int i = 0; i < 100'000; ++i) s += m.short_ascii;
                  keep(s.view());
              });
    bench.job("append 18 bytes mixed, 10,000 times", 50,
              [&] { std::string s; for (int i = 0; i < 10'000; ++i) s += short_mixed; keep(s); },
              [&](const auto& m) {
                  string_of<decltype(m)> s;
                  for (int i = 0; i < 10'000; ++i) s += m.short_mixed;
                  keep(s.view());
              });

    // --- One code point by its position ---
    bench.job("at(n) x1024, 4 KiB ASCII (std: s[n], right here)", 1000,
              [&] { std::size_t sum = 0; for (const std::size_t n : ascii_at) sum += static_cast<unsigned char>(std_ascii[n]); keep(sum); },
              [&](const auto& m) { std::size_t sum = 0; for (const std::size_t n : ascii_at) sum += m.ascii_4k.at(CodePointIndex{n})->size_utf8(); keep(sum); });
    bench.job("at(n) x1024, 4 KiB mixed (std: walks the UTF-8)", 100,
              [&] { std::size_t sum = 0; for (const std::size_t n : mixed_at) sum += std_offset_of(std_mixed, n); keep(sum); },
              [&](const auto& m) { std::size_t sum = 0; for (const std::size_t n : mixed_at) sum += m.mixed_4k.at(CodePointIndex{n})->size_utf8(); keep(sum); });
    bench.job("at(n) x1024, 4 KiB mixed (std: s[n], WRONG)", 1000,
              [&] { std::size_t sum = 0; for (const std::size_t n : mixed_at) sum += static_cast<unsigned char>(std_mixed[n]); keep(sum); },
              [&](const auto& m) { std::size_t sum = 0; for (const std::size_t n : mixed_at) sum += m.mixed_4k.at(CodePointIndex{n})->size_utf8(); keep(sum); });
    bench.job("at(n) x1024, 18 bytes mixed (std: walks)", 1000,
              [&] { std::size_t sum = 0; for (const std::size_t n : short_at) sum += std_offset_of(short_mixed, n); keep(sum); },
              [&](const auto& m) { std::size_t sum = 0; for (const std::size_t n : short_at) sum += m.short_mixed.at(CodePointIndex{n})->size_utf8(); keep(sum); });
    bench.job("at(n) x1024, view of 4 KiB mixed", 100,
              [&] { std::size_t sum = 0; for (const std::size_t n : mixed_at) sum += std_offset_of(std_mixed, n); keep(sum); },
              [&](const auto& m) {
                  std::size_t sum = 0;
                  const StringView<> view = m.mixed_4k.view();
                  for (const std::size_t n : mixed_at) sum += view.at(CodePointIndex{n})->size_utf8();
                  keep(sum);
              });
    bench.job("substring 100 code points x1024, 4 KiB mixed", 100,
              [&] {
                  std::size_t sum = 0;
                  const std::string_view all{std_mixed};
                  for (const std::size_t n : mixed_starts) {
                      const std::size_t begin = std_offset_of(all, n);
                      const std::size_t end = begin + std_offset_of(all.substr(begin), 100);
                      sum += all.substr(begin, end - begin).size();
                  }
                  keep(sum);
              },
              [&](const auto& m) {
                  std::size_t sum = 0;
                  for (const std::size_t n : mixed_starts) sum += m.mixed_4k.substring(CodePointIndex{n}, CodePointCount{100})->count_code_points();
                  keep(sum);
              });
    bench.job("replace 1 code point, 4 KiB mixed", 100'000,
              [&] {
                  std::string s{std_mixed};
                  const std::size_t begin = std_offset_of(s, 2000);
                  const std::size_t end = begin + std_offset_of(std::string_view{s}.substr(begin), 1);
                  s.replace(begin, end - begin, "X");
                  keep(s);
              },
              [&](const auto& m) { const auto s = m.mixed_4k.replace(CodePointIndex{2000}, CodePointCount{1}, "X"_js); keep(s->view()); });

    // --- Text going out: std::string already holds the packed bytes; jString packs its cells ---
    std::vector<char> out_buffer(mixed_4k.bytes.size());
    bench.job("pack 4 KiB mixed into a buffer (std: copy)", 100'000,
              [&] { std::ranges::copy(std_mixed, out_buffer.begin()); keep(std::string_view{out_buffer.data(), out_buffer.size()}); },
              [&](const auto& m) { keep(*m.mixed_4k.write_utf8(out_buffer)); });
    bench.job("pack 4 KiB ASCII to its own bytes (std: copy)", 100'000,
              [&] { const std::string t{std_ascii}; keep(t); },
              [&](const auto& m) { const std::vector<char> t = m.ascii_4k.to_utf8(); keep(std::string_view{t.data(), t.size()}); });

    // --- Counting, walking, searching, comparing, hashing ---
    bench.job("count code points, 4 KiB mixed", 100'000,
              [&] { keep(std_count(std_mixed)); },
              [&](const auto& m) { keep(m.mixed_4k.count_code_points()); });
    bench.job("count code points, view of 4 KiB mixed", 100'000,
              [&] { keep(std_count(std_mixed)); },
              [&](const auto& m) { keep(m.mixed_4k.view().count_code_points()); });
    bench.job("walk code points, 4 KiB mixed (std: bytes only)", 20'000,
              [&] {
                  std::size_t sum = 0;
                  for (const char unit : std_mixed) sum += static_cast<unsigned char>(unit);
                  keep(sum);
              },
              [&](const auto& m) { std::size_t sum = 0; for (const char32_t c : m.mixed_4k.code_points()) sum += c; keep(sum); });
    bench.job("contains, 4 KiB mixed, not there", 100'000,
              [&] { keep(std_mixed.find("zebra") == std::string::npos ? 1U : 0U); },
              [&](const auto& m) { keep(m.mixed_4k.contains("zebra"_js) ? 1U : 0U); });
    bench.job("compare equal, 4 KiB", 100'000,
              [&] { keep(std_mixed == std_mixed_copy ? 1U : 0U); },
              [&](const auto& m) { keep(m.mixed_4k == m.mixed_4k_copy ? 1U : 0U); });
    bench.job("hash, 4 KiB", 100'000,
              [&] { keep(std::hash<std::string>{}(std_mixed)); },
              [&](const auto& m) { keep(std::hash<std::remove_cvref_t<decltype(m.mixed_4k)>>{}(m.mixed_4k)); });

    std::cout << "\nsizeof: std::string " << sizeof(std::string) << " bytes; jstr::String<> " << sizeof(String<>) << " bytes; heap-only "
              << sizeof(HeapString) << " bytes; jstr::StringView " << sizeof(StringView<>) << " bytes\n";
    std::cout << "cells: 4 bytes per code point (4 KiB mixed: " << mixed_4k.code_points * 4 << " bytes, against " << mixed_4k.bytes.size()
              << " packed)\n";
    return 0;
}
