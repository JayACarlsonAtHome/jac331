# jString

A C++23 string library whose text is always valid in its encoding: UTF-8 unless you name another. It is built to be
hard to use wrong. A mistake is a compile error where it can be, and an empty answer (`std::optional` or
`std::expected`) where it can only be found at run time. It is never a wrong answer.

Header-only: `#include "jString/v001/jstring.hpp"` from `Lin/v001/include`.

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
