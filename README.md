# jString

A C++23 string library whose text is always valid in its encoding: UTF-8 unless you name another. It is built to be
hard to use wrong. A mistake is a compile error where it can be, and an empty answer (`std::optional` or
`std::expected`) where it can only be found at run time. It is never a wrong answer.

Header-only: `#include "jString/v001/jstring.hpp"` from `Lin/v001/include`.

## Compiler reports

Building jString and jCppGUI with every warning an error on both g++ and clang++, and turning none off, found three
compiler problems: two in clang++ 23's lifetime-safety analysis, and one where g++ rejects `[[clang::lifetimebound]]`
and so misses a dangling view that clang++ catches. Each is cut down to one small file in
[compiler_reports](compiler_reports/README.md), for the compilers' developers.

```cpp
#include "jString/v001/jstring.hpp"
using namespace jstr::literals;

jstr::String<> data = "This is a test"_js;                        // checked while compiling
if (data.at(jstr::CodePointNumber{7}) == U's') { /* ... */ }      // the 7th code point, counted from 1
auto word = data.substring(jstr::CodePointIndex{0}, jstr::CodePointCount{4});      // "This", counted from 0
auto fixed = data.replace(jstr::CodePointNumber{1}, jstr::CodePointCount{4}, "That");  // a new String

auto from_file = jstr::String<>::from(bytes);     // std::expected: valid text, or the offset of the first bad byte
auto shown = jstr::String<>::from_lossy(bytes);   // each bad sequence made U+FFFD
```

## Goals

- **Text is always valid.** A String cannot hold a broken UTF-8 sequence, so no code that reads one has to check.
- **Hard to use wrong.** A mistake is a compile error where it can be, an empty answer where it can only be found at
  run time, and never a wrong answer.
- **Count what people mean.** Positions and lengths are in code points, not bytes, and say which base they count from.
- **One encoding for everything stored: UTF-8, no exceptions.** Languages mix freely, and nothing has to be converted
  later because a narrower encoding was once thought to be enough.
- **No warnings, and none silenced.** Every warning gcc and clang can give is an error, and none is turned off with a
  pragma or a `-Wno-` flag. A design that sets off a false warning is changed, not hidden.
- **Fast where it counts.** Finding the nth code point, counting code points and taking a piece of the text should not
  cost a walk through the text.
- **Works while compiling.** Everything is `constexpr`, so it can be tested with `static_assert`.

## Design decisions

| Decision | Why |
|---|---|
| A template over the encoding (`String<E = Utf8>`), with only UTF-8 built | Room for another encoding later, without a second class. The `Encoding` concept says what one must give |
| Everything stored as UTF-8 | One form for every language. The table that makes `at()` fast is an index into the UTF-8, never a second copy in UTF-32 |
| Text under 64 units held inside the String (one cache line), longer text in a `std::vector` | Most text is short and needs no heap. 64 was measured against 80; `alignas(64)` was tried and gained nothing |
| The text's place worked out each time, never kept as a pointer | Nothing in a String points into the String, so a move cannot leave a pointer into the old one. `std::string`'s small-string buffer set off g++ 15's `-Wdangling-pointer`; this cannot |
| The count of code points kept, and kept up to date on every change | Counting is then free (0.09 ns, against 501 ns to count 4 KiB). Keeping it costs a count of only the text that was added |
| A table of where each code point starts, 4 bytes each: only for text on the heap that is not all ASCII, and at most 4 GiB | `at(n)` and `substring()` go straight to their place. ASCII needs no table (code point n is byte n), and short text is counted 16 bytes at a step |
| The table built byte by byte, 16 ASCII bytes at a step | A one-pass build sized from the count was tried and was slower (clang: 4.0 us to 5.5 us for 4 KiB) |
| No `operator[]`; `at()` gives the code point as an `optional` view | A byte index can land in the middle of a code point. Past the end is an empty answer, not a crash or a guess |
| `CodePointIndex` (from 0) and `CodePointNumber` (from 1) are types, and `CodePointCount` is another | A plain number, a base, or a start and a count swapped do not compile |
| Ways in: a checked literal, a named copy of a view, `from()` (an `expected`) or `from_lossy()` | Every copy is seen, and text from outside is checked once, where it comes in |
| Views from a temporary String do not compile | A view into text that is about to be destroyed is a dangling pointer by another name |
| `c_str()` gives nothing when the text holds a zero | C would stop at the zero and quietly lose the rest |
| A String moved from is empty | The standard says only "valid but unspecified" for `std::string`; here it is a promise |
| Header-only C++23: concepts, `std::expected`, `constexpr` | Nothing to build or link, and the compiler checks the rules |

## Pros and cons

What jString does better than `std::string`:

- Its text is always valid UTF-8; `std::string` holds any bytes and leaves checking to every reader.
- `at(n)` on 4 KiB of mixed text is about 860 times faster (1.1 us against 952 us for 1,024 of them), because
  `std::string` has to walk the UTF-8 to find a code point.
- Counting code points is free.
- Mistakes that `std::string` compiles (a byte index into UTF-8, a swapped start and count, a pointer into a temporary)
  are compile errors here; mistakes it can only find at run time give an empty answer instead of a wrong one.
- It sets off no false warnings: nothing in it points into itself.
- Searching, comparing and hashing are as fast as `std::string`'s.

What it costs:

- **Size.** A String is 128 bytes; `std::string` is 32. A class that holds many of them grows.
- **Making one is slower.** Every byte is checked, and mixed text on the heap also builds the table: 11 ns against
  1.9 ns for 10 bytes, 4.4 us against 29 ns for 4 KiB of mixed text.
- **Appending is slower:** about 6 times (876 us against 136 us for 100,000 appends of 10 bytes), because every
  added piece is checked and counted.
- **The table takes memory:** 4 bytes for every code point of mixed text on the heap, which for text that is mostly
  two- and three-byte characters is more than the text itself.
- **A code point is not always one character on the screen.** An accent written as a second code point, or a flag
  made of two, counts as two. jString does not handle grapheme clusters, normalization, case or locale.
- **Only UTF-8 is built.** The encoding is a template parameter, but no other encoding is written yet.
- **Crossing to code that uses `std::string`** means `from()` and a check of every byte each time text comes in.
- **A view into a String is good only until the String changes or moves**: short text moves with the String.
- **Needs a C++23 compiler:** g++ 15 or clang++ 21 and newer, and it is tested only on Linux.

## The types

| Type | What it is |
|---|---|
| `String<E = Utf8, InlineUnits = 64>` | Owned text: a value type that copies, moves (leaving the source empty), compares and hashes. Text under 64 units is held inside it, with no heap |
| `StringView<E = Utf8>` | Borrowed text, for parameters. Keep a `String`, pass a `StringView` |
| `Literal<E = Utf8>`, `"..."_js` | Text checked while compiling, which lives as long as the program |
| `CodePointPosition<IndexBase>` | A position counted in code points, from 0 (`CodePointIndex`, the default) or from 1 (`CodePointNumber`) |
| `CodePointCount` | A length in code points, so that a start and a length cannot be swapped |
| `IndexBase` | `zero_based` or `one_based`: a scoped enum, never a number |
| `Encoding` | The concept an encoding meets: a code unit, `scan()` and `replacement()` |

## What does not compile

Each of these has a test in `Lin/v001/tests/compile_fail`. Each test also checks that the same file compiles once the
mistake is taken out.

- making a String from a `char*` or `std::string` without `from()`, or from a view without naming the copy
- an invalid UTF-8 literal, or a run-time array passed as a literal
- `s[0]`: a byte index can land inside a code point
- ignoring `from()`'s answer
- a plain number as a position, a base or a code point; a start and a count swapped
- a view into a temporary String

## How fast

A String knows how many code points it holds, so `count_code_points()` costs nothing. For text on the heap that is not
all ASCII it also keeps a table of where each code point starts, 4 bytes each, so `at()`, `substring()` and `replace()`
find their place at once instead of walking the text. ASCII needs no table: code point n is byte n. Short text is held
inside the String, never on the heap, and is short enough to count in a few steps. A `StringView` keeps no table, and
counts 16 bytes at a step.

What that costs: making a String checks every byte (`std::string` checks none), and for mixed text on the heap also
builds the table, which is 4 bytes per code point. A String is 128 bytes; `std::string` is 32.

`bench/bench_string.cpp` times each job both ways. Build `gcc-release` or `clang-release` and run
`build/<preset>/bench_string`, pinned to one core (`taskset -c 5`) for steady numbers. On the host (Core Ultra 7 265,
g++ 16), per job:

| Job | `std::string` | jString |
|---|---|---|
| 1,024 `at(n)` on 4 KiB of mixed text (`std::string` walks the UTF-8) | 952 us | 1.1 us |
| 1,024 substrings of 100 code points, the same text | 968 us | 1.1 us |
| Count the code points of 4 KiB of mixed text | 501 ns | 0.09 ns |
| Make from 10 bytes | 1.9 ns | 11 ns |
| Make from 4 KiB of ASCII | 29 ns | 440 ns |
| Make from 4 KiB of mixed text | 29 ns | 4.4 us |
| Append 10 bytes, 100,000 times | 136 us | 876 us |
| Search, compare, hash 4 KiB | the same | the same |

## Building

From `Lin/v001`: `bash bsir.sh` installs what is missing, then builds and tests with this machine's pinned compilers
(`../../../Compiler_Pins/compilers.pin.jtext`, shared with jCppGUI and jac313):

| Preset | What it does |
|---|---|
| `gcc-debug`, `clang-debug` | Build and run every test, with every warning an error (clang adds `-Weverything`) |
| `gcc-warn` | Optimized, without the hardening flags that hide some warnings: build only |
| `gcc-asan` | AddressSanitizer and UndefinedBehaviorSanitizer |
| `gcc-release`, `clang-release` | Optimized, with the hardening: for reading the benchmark's numbers |

Tested on Fedora 45 (gcc 16, clang 23) and Linux Mint 22 (gcc 15, clang 21).
