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
| `String<E = Utf8>` | Owned text: a value type that copies, moves (leaving the source empty), compares and hashes |
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

## Building

From `Lin/v001`: `bash bsir.sh` installs what is missing, then builds and tests with this machine's pinned compilers
(`../../../Compiler_Pins/compilers.pin.jtext`, shared with jCppGUI and jac313):

| Preset | What it does |
|---|---|
| `gcc-debug`, `clang-debug` | Build and run every test, with every warning an error (clang adds `-Weverything`) |
| `gcc-warn` | Optimized, without the hardening flags that hide some warnings: build only |
| `gcc-asan` | AddressSanitizer and UndefinedBehaviorSanitizer |

Tested on Fedora 45 (gcc 16, clang 23) and Linux Mint 22 (gcc 15, clang 21).
