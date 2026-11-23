// File: bench/bench_string.cpp
// Purpose: Times jString in each of its five Types against std::string doing the same jobs, and prints one table, so
//          that which Type is fast at what can be read off a row. Each Type makes text from its own units (bytes for
//          Ascii, UTF8Packed and UTF8Full; char16_t for UTF16; char32_t for UTF32), and writes UTF-8 out, which every
//          Type but UTF8Packed and Ascii must convert to. Ascii cannot hold the mixed text, so it has a - in those rows.
//          Where std::string has no answer of its own (the 500th code point, a piece counted in code points), it is
//          given the plain correct way: walking the UTF-8, as a programmer would write it. Byte indexing is timed
//          separately and labelled, because it is fast and, for text that is not ASCII, wrong.
//
//          Each job runs in rounds of many repetitions. The time shown is the fastest round's, per repetition: the
//          number least disturbed by whatever else the machine is doing. Every string made has its address and some of
//          its units stored where the compiler must assume they are read, so that no compiler can leave out making it
//          (clang++ did, for std::string, until this was done).
//
//          Build it optimized (gcc-release or clang-release) for numbers worth reading. Any argument (ctest passes
//          --quick) runs every job a few times only, to prove it works.
// Modified 2026-11-23 by Claude Opus 5.5: [[clang::lifetimebound]] where clang++ 23 asks for it.
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

using jString::CodePointCount;
using jString::CodePointIndex;
using jString::String;
using jString::StringView;
using jString::Type;
using namespace jString::literals;

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

// A jString made: its first and last code points, and a size, so that no compiler can leave out filling it. Each is
// found without walking the text, in every Type, so that keeping a result costs the same in each and is never what is
// timed: the last code point in the Types found by multiplying, the size in units in the two that walk.
template <Type T>
void keep(StringView<T> made) {
    std::size_t seen = 0;
    if (!made.empty()) seen += *made.code_points().begin();
    if constexpr (T == Type::UTF8Packed || T == Type::UTF16) {
        seen += made.size_packed();
    } else {
        const std::size_t count = made.count_code_points();
        seen += count;
        if (count != 0) seen += *made.at(CodePointIndex{count - 1})->code_points().begin();
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

// Valid UTF-8 as T's own units: what T makes text from.
template <Type T>
[[nodiscard]] std::basic_string<typename StringView<T>::unit> units_as(std::string_view utf8) {
    const String<Type::UTF8Packed> text = String<Type::UTF8Packed>::from(utf8).value();
    if constexpr (T == Type::Ascii) {
        const std::vector<char> plain = jString::convert_lossy<Type::Ascii>(text).to_packed();
        return std::string{plain.begin(), plain.end()};
    } else {
        const auto packed = jString::convert<T>(text).to_packed();
        return std::basic_string<typename StringView<T>::unit>{packed.begin(), packed.end()};
    }
}

// The texts every job reads, in one Type: as its own units, and made.
template <Type T>
struct Made {
    using string = String<T>;
    using units = std::basic_string<typename StringView<T>::unit>;
    units short_ascii_in;
    units short_mixed_in;
    units medium_ascii_in;
    units ascii_4k_in;
    units mixed_4k_in;
    units mixed_1m_in;
    String<T> short_ascii;
    String<T> short_mixed;
    String<T> ascii_4k;
    String<T> mixed_4k;
    String<T> mixed_4k_copy;
};

template <Type T>
[[nodiscard]] Made<T> made_as(std::string_view short_ascii, std::string_view short_mixed, std::string_view medium_ascii, const Text& ascii_4k,
                              const Text& mixed_4k, const Text& mixed_1m) {
    Made<T> made{units_as<T>(short_ascii), units_as<T>(short_mixed), units_as<T>(medium_ascii), units_as<T>(ascii_4k.bytes), units_as<T>(mixed_4k.bytes),
                 units_as<T>(mixed_1m.bytes), {}, {}, {}, {}, {}};
    made.short_ascii = String<T>::from(made.short_ascii_in).value();
    made.short_mixed = String<T>::from(made.short_mixed_in).value();
    made.ascii_4k = String<T>::from(made.ascii_4k_in).value();
    made.mixed_4k = String<T>::from(made.mixed_4k_in).value();
    made.mixed_4k_copy = made.mixed_4k;
    return made;
}

template <class M>
using string_of = typename std::remove_cvref_t<M>::string;

// Whether a job's text is all ASCII, so that Ascii can do it too.
enum class Holds : unsigned char { ascii, mixed };

class Bench {
public:
    Bench(bool quick, const Made<Type::Ascii>& ascii [[clang::lifetimebound]],
          const Made<Type::UTF8Packed>& packed [[clang::lifetimebound]], const Made<Type::UTF8Full>& full [[clang::lifetimebound]],
          const Made<Type::UTF16>& utf16 [[clang::lifetimebound]], const Made<Type::UTF32>& utf32 [[clang::lifetimebound]])
        : quick_{quick}, ascii_{ascii}, packed_{packed}, full_{full}, utf16_{utf16}, utf32_{utf32} {}

    // Times one job: std::string's way, then each Type's, given that Type's texts.
    template <class Std, class Jstr>
    void job(std::string_view name, Holds holds, std::size_t reps, const Std& with_std, const Jstr& with_jstr) const {
        const double std_ns = time(reps, with_std);
        std::cout << std::left << std::setw(48) << name << std::right << std::setw(10) << format(std_ns);
        if (holds == Holds::ascii) {
            std::cout << std::setw(10) << format(time(reps, [&] { with_jstr(ascii_); }));
        } else {
            std::cout << std::setw(10) << "-";
        }
        std::cout << std::setw(10) << format(time(reps, [&] { with_jstr(packed_); })) << std::setw(10) << format(time(reps, [&] { with_jstr(full_); }))
                  << std::setw(10) << format(time(reps, [&] { with_jstr(utf16_); })) << std::setw(10) << format(time(reps, [&] { with_jstr(utf32_); }))
                  << '\n';
    }

    static void heading() {
        std::cout << std::left << std::setw(48) << "job (time for one)" << std::right << std::setw(10) << "std" << std::setw(10) << "Ascii" << std::setw(10)
                  << "UTF8Pack" << std::setw(10) << "UTF8Full" << std::setw(10) << "UTF16" << std::setw(10) << "UTF32" << '\n';
        std::cout << std::string(108, '-') << '\n';
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

    bool quick_;
    const Made<Type::Ascii>& ascii_;
    const Made<Type::UTF8Packed>& packed_;
    const Made<Type::UTF8Full>& full_;
    const Made<Type::UTF16>& utf16_;
    const Made<Type::UTF32>& utf32_;
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

    const Made<Type::Ascii> ascii = made_as<Type::Ascii>(short_ascii, short_mixed, medium_ascii, ascii_4k, mixed_4k, mixed_1m);
    const Made<Type::UTF8Packed> packed = made_as<Type::UTF8Packed>(short_ascii, short_mixed, medium_ascii, ascii_4k, mixed_4k, mixed_1m);
    const Made<Type::UTF8Full> full = made_as<Type::UTF8Full>(short_ascii, short_mixed, medium_ascii, ascii_4k, mixed_4k, mixed_1m);
    const Made<Type::UTF16> utf16 = made_as<Type::UTF16>(short_ascii, short_mixed, medium_ascii, ascii_4k, mixed_4k, mixed_1m);
    const Made<Type::UTF32> utf32 = made_as<Type::UTF32>(short_ascii, short_mixed, medium_ascii, ascii_4k, mixed_4k, mixed_1m);
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
    const Bench bench{quick, ascii, packed, full, utf16, utf32};
    constexpr Holds plain = Holds::ascii;
    constexpr Holds wide = Holds::mixed;

    // --- Making text from its own units: std::string copies, jString checks and copies ---
    bench.job("make from 10 bytes ASCII", plain, 2'000'000,
              [&] { const std::string s{short_ascii}; keep(s); },
              [&](const auto& m) { const auto s = string_of<decltype(m)>::from(m.short_ascii_in); keep(s->view()); });
    bench.job("make from 18 bytes mixed", wide, 2'000'000,
              [&] { const std::string s{short_mixed}; keep(s); },
              [&](const auto& m) { const auto s = string_of<decltype(m)>::from(m.short_mixed_in); keep(s->view()); });
    bench.job("make from 60 bytes ASCII", plain, 2'000'000,
              [&] { const std::string s{medium_ascii}; keep(s); },
              [&](const auto& m) { const auto s = string_of<decltype(m)>::from(m.medium_ascii_in); keep(s->view()); });
    bench.job("make from 4 KiB ASCII", plain, 100'000,
              [&] { const std::string s{ascii_4k.bytes}; keep(s); },
              [&](const auto& m) { const auto s = string_of<decltype(m)>::from(m.ascii_4k_in); keep(s->view()); });
    bench.job("make from 4 KiB mixed", wide, 100'000,
              [&] { const std::string s{mixed_4k.bytes}; keep(s); },
              [&](const auto& m) { const auto s = string_of<decltype(m)>::from(m.mixed_4k_in); keep(s->view()); });
    bench.job("make from 1 MiB mixed", wide, 200,
              [&] { const std::string s{mixed_1m.bytes}; keep(s); },
              [&](const auto& m) { const auto s = string_of<decltype(m)>::from(m.mixed_1m_in); keep(s->view()); });
    bench.job("make from 4 KiB mixed, lossy (all valid)", wide, 100'000,
              [&] { const std::string s{mixed_4k.bytes}; keep(s); },
              [&](const auto& m) { const auto s = string_of<decltype(m)>::from_lossy(m.mixed_4k_in); keep(s.view()); });

    // --- Copying ---
    bench.job("copy 10 bytes", plain, 2'000'000,
              [&] { const std::string t{std_short}; keep(t); },
              [&](const auto& m) { const string_of<decltype(m)> t{m.short_ascii}; keep(t.view()); });
    bench.job("copy 4 KiB ASCII", plain, 100'000,
              [&] { const std::string t{std_ascii}; keep(t); },
              [&](const auto& m) { const string_of<decltype(m)> t{m.ascii_4k}; keep(t.view()); });
    bench.job("copy 4 KiB mixed", wide, 100'000,
              [&] { const std::string t{std_mixed}; keep(t); },
              [&](const auto& m) { const string_of<decltype(m)> t{m.mixed_4k}; keep(t.view()); });

    // --- Appending ---
    bench.job("append 10 bytes, 100,000 times (one = all)", plain, 20,
              [&] { std::string s; for (int i = 0; i < 100'000; ++i) s += std_short; keep(s); },
              [&](const auto& m) {
                  string_of<decltype(m)> s;
                  for (int i = 0; i < 100'000; ++i) s += m.short_ascii;
                  keep(s.view());
              });
    bench.job("append 18 bytes mixed, 10,000 times", wide, 50,
              [&] { std::string s; for (int i = 0; i < 10'000; ++i) s += short_mixed; keep(s); },
              [&](const auto& m) {
                  string_of<decltype(m)> s;
                  for (int i = 0; i < 10'000; ++i) s += m.short_mixed;
                  keep(s.view());
              });

    // --- One code point by its position ---
    bench.job("at(n) x1024, 4 KiB ASCII (std: s[n], right here)", plain, 1000,
              [&] { std::size_t sum = 0; for (const std::size_t n : ascii_at) sum += static_cast<unsigned char>(std_ascii[n]); keep(sum); },
              [&](const auto& m) { std::size_t sum = 0; for (const std::size_t n : ascii_at) sum += *m.ascii_4k.at(CodePointIndex{n})->code_points().begin(); keep(sum); });
    bench.job("at(n) x1024, 4 KiB mixed (std: walks the UTF-8)", wide, 100,
              [&] { std::size_t sum = 0; for (const std::size_t n : mixed_at) sum += std_offset_of(std_mixed, n); keep(sum); },
              [&](const auto& m) { std::size_t sum = 0; for (const std::size_t n : mixed_at) sum += *m.mixed_4k.at(CodePointIndex{n})->code_points().begin(); keep(sum); });
    bench.job("at(n) x1024, 4 KiB mixed (std: s[n], WRONG)", wide, 1000,
              [&] { std::size_t sum = 0; for (const std::size_t n : mixed_at) sum += static_cast<unsigned char>(std_mixed[n]); keep(sum); },
              [&](const auto& m) { std::size_t sum = 0; for (const std::size_t n : mixed_at) sum += *m.mixed_4k.at(CodePointIndex{n})->code_points().begin(); keep(sum); });
    bench.job("at(n) x1024, 18 bytes mixed (std: walks)", wide, 1000,
              [&] { std::size_t sum = 0; for (const std::size_t n : short_at) sum += std_offset_of(short_mixed, n); keep(sum); },
              [&](const auto& m) { std::size_t sum = 0; for (const std::size_t n : short_at) sum += *m.short_mixed.at(CodePointIndex{n})->code_points().begin(); keep(sum); });
    bench.job("substring 100 code points x1024, 4 KiB mixed", wide, 100,
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
                  for (const std::size_t n : mixed_starts) sum += m.mixed_4k.substring(CodePointIndex{n}, CodePointCount{100})->empty() ? 0U : 1U;
                  keep(sum);
              });
    bench.job("replace 1 code point, 4 KiB mixed", wide, 100'000,
              [&] {
                  std::string s{std_mixed};
                  const std::size_t begin = std_offset_of(s, 2000);
                  const std::size_t end = begin + std_offset_of(std::string_view{s}.substr(begin), 1);
                  s.replace(begin, end - begin, "X");
                  keep(s);
              },
              [&](const auto& m) { const auto s = m.mixed_4k.replace(CodePointIndex{2000}, CodePointCount{1}, "X"_js); keep(s->view()); });

    // --- Text going out as UTF-8: std::string already holds it; UTF8Full packs its cells, UTF16 and UTF32 convert ---
    std::vector<char> out_buffer(mixed_4k.bytes.size());
    bench.job("UTF-8 of 4 KiB mixed into a buffer (std: copy)", wide, 100'000,
              [&] { std::ranges::copy(std_mixed, out_buffer.begin()); keep(std::string_view{out_buffer.data(), out_buffer.size()}); },
              [&](const auto& m) { keep(*m.mixed_4k.write_utf8(out_buffer)); });
    bench.job("UTF-8 of 4 KiB ASCII, its own (std: copy)", plain, 100'000,
              [&] { const std::string t{std_ascii}; keep(t); },
              [&](const auto& m) { const std::vector<char> t = m.ascii_4k.to_utf8(); keep(std::string_view{t.data(), t.size()}); });

    // --- Counting, walking, searching, comparing, hashing ---
    bench.job("count code points, 4 KiB mixed", wide, 100'000,
              [&] { keep(std_count(std_mixed)); },
              [&](const auto& m) { keep(m.mixed_4k.count_code_points()); });
    bench.job("walk code points, 4 KiB mixed (std: bytes only)", wide, 20'000,
              [&] {
                  std::size_t sum = 0;
                  for (const char unit : std_mixed) sum += static_cast<unsigned char>(unit);
                  keep(sum);
              },
              [&](const auto& m) { std::size_t sum = 0; for (const char32_t c : m.mixed_4k.code_points()) sum += c; keep(sum); });
    bench.job("contains, 4 KiB mixed, not there", wide, 100'000,
              [&] { keep(std_mixed.find("zebra") == std::string::npos ? 1U : 0U); },
              [&](const auto& m) { keep(m.mixed_4k.contains("zebra"_js) ? 1U : 0U); });
    bench.job("compare equal, 4 KiB", wide, 100'000,
              [&] { keep(std_mixed == std_mixed_copy ? 1U : 0U); },
              [&](const auto& m) { keep(m.mixed_4k == m.mixed_4k_copy ? 1U : 0U); });
    bench.job("hash, 4 KiB", wide, 100'000,
              [&] { keep(std::hash<std::string>{}(std_mixed)); },
              [&](const auto& m) { keep(std::hash<std::remove_cvref_t<decltype(m.mixed_4k)>>{}(m.mixed_4k)); });

    std::cout << "\nsizeof: std::string " << sizeof(std::string) << " bytes; jString::String (any Type) " << sizeof(String<Type::UTF8Full>)
              << " bytes, 64 of them inside; jString::StringView " << sizeof(StringView<Type::UTF8Full>) << " bytes\n";
    std::cout << "4 KiB mixed kept: std::string " << mixed_4k.bytes.size() << " bytes; UTF8Packed " << packed.mixed_4k.size_packed() << "; UTF8Full "
              << mixed_4k.code_points * 4 << "; UTF16 " << utf16.mixed_4k.size_packed() * 2 << "; UTF32 " << mixed_4k.code_points * 4 << "\n";
    return 0;
}
