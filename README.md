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
auto fixed = data.replace(jstr::CodePointNumber{1}, jstr::CodePointCount{4}, "That"_js);  // a new jString

auto from_file = jstr::String<>::from(bytes);     // std::expected: valid text, or the offset of the first bad byte
auto shown = jstr::String<>::from_lossy(bytes);   // each bad sequence made U+FFFD
std::vector<char> out = data.to_utf8();           // the one way out: packed UTF-8, by name

jstr::FixedString<43> payload;                    // at most 43 code points, all inside: no heap, no destructor
payload.assign_lossy(bytes);                      // made U+FFFD where bad, cut after 43
```

## Goals

- **Text is always valid.** A jString cannot hold a broken UTF-8 sequence, so no code that reads one has to check.
- **Hard to use wrong.** A mistake is a compile error where it can be, an empty answer where it can only be found at
  run time, and never a wrong answer.
- **Count what people mean.** Positions and lengths are in code points, not bytes, and say which base they count from.
- **One encoding for everything stored: UTF-8, no exceptions.** Languages mix freely, and nothing has to be converted
  later because a narrower encoding was once thought to be enough.
- **Simple and fast before small.** Every code point takes the same room, so finding one is a multiplication. Space is
  traded for speed and simplicity, and there is plenty of space.
- **No warnings, and none silenced.** Every warning gcc and clang can give is an error, and none is turned off with a
  pragma or a `-Wno-` flag. A design that sets off a false warning is changed, not hidden.
- **Works while compiling.** Everything is `constexpr`, so it can be tested with `static_assert`.

## How text is kept: a cell to each code point

Every code point takes one **cell** of 4 bytes: its UTF-8 bytes, then zeros.

| Code point | Its cell |
|---|---|
| `h` (U+0068) | `68 00 00 00` |
| `é` (U+00E9) | `C3 A9 00 00` |
| `日` (U+65E5) | `E6 97 A5 00` |
| `😀` (U+1F600) | `F0 9F 98 80` |

- **Code point n is cell n.** `at()`, `substring()` and `replace()` find their place by multiplying, with no table and no
  walk through the text. Counting code points is dividing.
- **Still UTF-8.** A cell holds the code point's own UTF-8 bytes, so the rule above holds: nothing is stored in any
  other encoding. Text goes out by dropping the zeros.
- **The same on every machine.** A cell is four bytes in order, not a 4-byte number, so it does not depend on the
  machine's byte order (little- or big-endian). Cells are compared and searched as bytes, never loaded as numbers:
  compared as bytes, cells sort in code point order on every machine.
- **Never seen as bytes.** A view of cells is a `jstr::StringView`, never a `std::string_view`: read as plain bytes, a
  cell's zeros would look like text. There is no `units()`. Text leaves jString only packed, by name: `to_utf8()`,
  `write_utf8(buffer)`, `to_c_string()`, `<<` and `std::format`.

## Design decisions

| Decision | Why |
|---|---|
| A fixed 4-byte cell for every code point, holding its UTF-8 bytes then zeros | Simple and fast: code point n is at n × 4, nothing has to be walked, indexed or kept in a table. ASCII takes 4 bytes, not 1: space for speed, on purpose |
| The cell holds UTF-8 bytes, not the code point as a 32-bit number (UTF-32) | Bytes in order read the same on every machine; a number depends on the byte order. And everything stored stays UTF-8 |
| Cells compared and searched as bytes, never as numbers | Byte order is code point order on every machine; a loaded number's order is not. A search match must start a cell, so U+0000 (all zeros) never matches a cell's padding |
| `jstr::StringView`, never `std::string_view`, for text | A `std::string_view` of cells would read the zeros as text. Text goes out packed, by name |
| A plain `"text"` is not text: literals are `"text"_js` | A plain literal is packed bytes with no cells. `"text"_js` has its cells made and checked while compiling, kept for the whole run |
| Text from outside always comes in through `String::from()` (or `from_lossy()`), which copies it into cells | A view cannot look at packed bytes, so there is no `StringView::from`. Checking and spreading into cells is one pass |
| A template over the encoding (`jstr::String<E = Utf8>`), with only UTF-8 built | Room for another encoding later, without a second class. The `Encoding` concept says what one must give, the cell size among it |
| Up to 16 code points (64 bytes, one cache line) held inside a `jstr::String`, longer text in a `std::vector` | Most short text needs no heap. The place is worked out each time, never kept as a pointer, so a move cannot leave a pointer into the old String. `std::string`'s small-string buffer set off g++ 15's `-Wdangling-pointer`; this cannot |
| `jstr::FixedString<N>`: at most N code points, all inside, no heap | Trivially copyable and destructible, and all zeros is the empty text: a block of them (a store's rows) needs no construction, destruction or clearing. Only the first count cells are ever read |
| No `operator[]`; `at()` gives the code point as an `optional` view | Past the end is an empty answer, not a crash or a guess |
| `CodePointIndex` (from 0) and `CodePointNumber` (from 1) are types, and `CodePointCount` is another | A plain number, a base, or a start and a count swapped do not compile |
| Views from a temporary jString do not compile | A view into text that is about to be destroyed is a dangling pointer by another name |
| `to_c_string()` gives nothing when the text holds a zero | C would stop at the zero and quietly lose the rest |
| A jString moved from is empty | The standard says only "valid but unspecified" for `std::string`; here it is a promise |
| Header-only C++23: concepts, `std::expected`, `constexpr` | Nothing to build or link, and the compiler checks the rules |

## The types

| Type | What it is |
|---|---|
| `jstr::String<E = Utf8, InlineCells = 16>` | Owned text: a value type that copies, moves (leaving the source empty), compares and hashes. Up to 16 code points are held inside it, with no heap |
| `jstr::FixedString<N, E = Utf8>` | Owned text of at most N code points, all inside: no heap, no destructor, all zeros is empty. For rows of a store |
| `jstr::StringView<E = Utf8>` | Borrowed text, a window onto cells: a pointer and a size, 16 bytes. Keep a `jstr::String`, pass a `jstr::StringView` |
| `Literal<E = Utf8>`, `"..."_js` | Text checked while compiling, its cells made then, which lives as long as the program |
| `CodePointPosition<IndexBase>` | A position counted in code points, from 0 (`CodePointIndex`, the default) or from 1 (`CodePointNumber`) |
| `CodePointCount` | A length in code points, so that a start and a length cannot be swapped |
| `IndexBase` | `zero_based` or `one_based`: a scoped enum, never a number |
| `Encoding` | The concept an encoding meets: a code unit, a cell size, `scan()`, `replacement()`, `decode()` |

## What does not compile

Each of these has a test in `Lin/v001/tests/compile_fail`. Each test also checks that the same file compiles once the
mistake is taken out.

- making a jString from a `char*` or `std::string` without `from()`, or from a view without naming the copy
- a plain `"text"` where text is wanted: it is `"text"_js`
- an invalid UTF-8 literal, or a run-time array passed as a literal
- text turned into a `std::string_view`, or its cells asked for as units (there is no `units()`)
- `s[0]`
- ignoring `from()`'s answer
- a plain number as a position, a base or a code point; a start and a count swapped
- a view into a temporary jString

## How fast

Timed with `bench/bench_string.cpp` on the host (Core Ultra 7 265, g++ 16, one core, release flags with the
hardening), against the packed jString it replaced (commit 8d39622) and `std::string`:

| Job | `std::string` | packed jString | jString (cells) |
|---|---|---|---|
| 1,024 `at(n)` on 18 bytes of mixed text | 3.2 us | 18.9 us | 0.5 us |
| 1,024 `at(n)` on a view of 4 KiB of mixed text | 952 us | 218 us | 0.4 us |
| 1,024 substrings of 100 code points, 4 KiB mixed | 948 us | 1.1 us | 0.5 us |
| Replace one code point in 4 KiB mixed | 1.1 us | 2.1 us | 0.12 us |
| Count the code points of a view of 4 KiB mixed | 500 ns | 349 ns | 0.2 ns |
| Append 18 bytes of mixed text, 10,000 times | 17 us | 207 us | 59 us |
| Make from 4 KiB of mixed text | 30 ns | 4.4 us | 4.5 us |
| Make from 4 KiB of ASCII | 30 ns | 0.44 us | 0.57 us |
| Pack 4 KiB of mixed text back to UTF-8 | (a copy, 24 ns) | (none needed) | 2.0 us |
| Search, compare, hash 4 KiB | 16, 25, 384 ns | the same | about 3 times: 48, 61, 1171 ns |

Finding a code point by position, counting and replacing are much faster. Work that reads every byte (search,
compare, hash, copy) is about 3 times slower, because there are about 3 times the bytes. Text going out is packed,
which `std::string` never has to do. A `jstr::String` is 96 bytes; `std::string` is 32.

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
