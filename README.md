# jString

A C++23 string library whose text is always valid. Every jString names its **Type**: how its text is kept, out of
five (ASCII, UTF-8 packed, UTF-8 in 4-byte cells, UTF-16, UTF-32), with no default. It is built to be hard to use
wrong. A mistake is a compile error where it can be, and an empty answer (`std::optional` or `std::expected`) where it
can only be found at run time. It is never a wrong answer.

Header-only: `#include "jString/v001/jstring.hpp"` from `Lin/v001/include`.
Everything is in the namespace `jString`; `jstr` is a short name for it (`namespace jstr = jString;`), so
`jstr::String<jstr::Type::UTF8Packed>` is the same type. `jstr_vw<T>` is the short name of `jString::StringView<T>`.

## Compiler reports

Building jString and jCppGUI with every warning an error on both g++ and clang++, and turning none off, found three
compiler problems: two in clang++ 23's lifetime-safety analysis, and one where g++ rejects `[[clang::lifetimebound]]`
and so misses a dangling view that clang++ catches. Each is cut down to one small file in
[compiler_reports](compiler_reports/README.md), for the compilers' developers.

```cpp
#include "jString/v001/jstring.hpp"
using namespace jString::literals;
using jString::Type;

jString::String<Type::UTF8Full> data = "This is a test"_js;          // checked while compiling
if (data.at(jString::CodePointNumber{7}) == U's') { /* ... */ }      // the 7th code point, counted from 1
auto word = data.substring(jString::CodePointIndex{0}, jString::CodePointCount{4});      // "This", counted from 0
auto fixed = data.replace(jString::CodePointNumber{1}, jString::CodePointCount{4}, "That"_js);  // a new jString

auto from_file = jString::String<Type::UTF8Packed>::from(bytes);   // std::expected: valid text, or where it first goes wrong
auto shown = jString::String<Type::UTF8Packed>::from_lossy(bytes); // each bad sequence made U+FFFD
std::vector<char> out = data.to_utf8();                         // out by name, as UTF-8, whatever the Type

auto for_windows = jString::convert<Type::UTF16>(data);            // another Type, by name; it cannot fail
auto for_a_header = jString::convert<Type::Ascii>(data);           // it can: std::expected, saying which code point

jString::FixedString<Type::UTF8Full, 43> payload;  // at most 43 code points, all inside: no heap, no destructor
payload.assign_lossy(bytes);                    // made U+FFFD where bad, cut after 43
```

## Goals

- **Text is always valid.** A jString cannot hold a broken sequence, so no code that reads one has to check.
- **Hard to use wrong.** A mistake is a compile error where it can be, an empty answer where it can only be found at
  run time, and never a wrong answer.
- **Count what people mean.** Positions and lengths are in code points, not units, and say which base they count from.
- **Choose how text is kept, on purpose.** Each Type is fast at different work. The choice is named in the type,
  never defaulted, so it is always a decision and always visible.
- **Types never mix by accident.** Text of one Type meets another only through `convert<To>()`.
- **No warnings, and none silenced.** Every warning gcc and clang can give is an error, and none is turned off with a
  pragma or a `-Wno-` flag. A design that sets off a false warning is changed, not hidden.
- **Works while compiling.** Everything is `constexpr`, so it can be tested with `static_assert`.

## The five Types

| `jString::Type::` | Unit | One code point takes | Code point n is found by | Holds |
|---|---|---|---|---|
| `Ascii` | `char` | 1 byte | multiplying | U+0000 to U+007F only |
| `UTF8Packed` | `char` | 1 to 4 bytes, as files hold it | walking the text | every code point |
| `UTF8Full` | `char` | 4 bytes: its UTF-8, then zeros (a cell) | multiplying | every code point |
| `UTF16` | `char16_t` | 1 or 2 units (a pair above U+FFFF) | walking the text | every code point |
| `UTF32` | `char32_t` | 1 unit: the code point itself | multiplying | every code point |

Every Type compares in code point order on every machine: Ascii, UTF8Packed and UTF8Full as bytes, UTF32 as numbers,
and UTF16 by a rank that puts a pair above every single unit. Plain UTF-16 unit order would put U+FF61 after
U+10000, which is wrong.

### When to use each

**`Ascii`**: text that is ASCII by rule, not by luck.
- protocol keywords, HTTP header names, JSON keys you define, command names, enum names, identifiers
- hex, Base64, numbers formatted for a file, ISO dates, log levels
- the smallest and fastest Type: one byte, found by multiplying, and already valid UTF-8, so it goes out with no work
- never for anything a person types or names: a name with é in it is refused (`from()`) or made `?` (`from_lossy()`)

**`UTF8Packed`**: text that mostly passes through.
- reading a file and writing it out again; log lines; network messages; configuration values
- keys of a map or set: compare, hash and search are as fast as `std::string`'s
- the smallest form of any text, and the form files, terminals and Linux take, so it goes out with no work
- slow at finding the nth code point: it walks the text to get there

**`UTF8Full`**: text that is edited or worked on by position.
- text in an editor, a field being typed into, a grid cell, a cursor that moves by code point
- `at()`, `substring()`, `replace()` and counting are a multiplication, whatever the text holds
- rows of a store (`FixedString`): every row the same size, all zeros is empty
- costs 4 bytes a code point, about 3 times UTF-8 for European text, so search, compare and hash read about 3 times
  the bytes, and text must be packed to go out

**`UTF16`**: text going to or coming from something that speaks UTF-16.
- Windows calls (`wchar_t` there is UTF-16), Java, JavaScript, .NET, Qt's `QString`, ICU
- 2 bytes for most of the world's scripts (CJK takes 3 in UTF-8), so East Asian text is smaller than in UTF-8
- slow at finding the nth code point, like UTF8Packed: a pair is two units
- kept in this machine's byte order

**`UTF32`**: text worked on one code point at a time, as numbers.
- Unicode work: looking up properties, normalizing, case mapping, shaping, sorting by rule
- the code point is the unit: nothing is decoded, and finding the nth is a multiplication
- APIs that take a code point as a number, such as FreeType's glyph lookup
- 4 bytes a code point, like UTF8Full, but going out as UTF-8 means encoding each one
- kept in this machine's byte order

**Not sure?** Use `UTF8Full` for text people edit, `UTF8Packed` for text that passes through, and convert at the
boundary where one becomes the other.

## How UTF8Full keeps text: a cell to each code point

Every code point takes one **cell** of 4 bytes: its UTF-8 bytes, then zeros.

| Code point | Its cell |
|---|---|
| `h` (U+0068) | `68 00 00 00` |
| `é` (U+00E9) | `C3 A9 00 00` |
| `日` (U+65E5) | `E6 97 A5 00` |
| `😀` (U+1F600) | `F0 9F 98 80` |

- **Code point n is cell n.** `at()`, `substring()` and `replace()` find their place by multiplying, with no table and no
  walk through the text. Counting code points is dividing.
- **Still UTF-8.** A cell holds the code point's own UTF-8 bytes. Text goes out by dropping the zeros.
- **The same on every machine.** A cell is four bytes in order, not a 4-byte number, so it does not depend on the
  machine's byte order (little- or big-endian). Compared as bytes, cells sort in code point order on every machine. A
  search match must start a cell, so U+0000 (all zeros) never matches a cell's padding.

## Text never leaves as units

A `jString::StringView` is never a `std::string_view`, `std::u16string_view`, `std::span` or pointer: no conversion
exists, not even an explicit one, and `string.hpp` proves it with a `static_assert` in every build. Read as plain
bytes, UTF8Full's zeros would look like text. Text leaves jString only by name:

| Out | What it gives |
|---|---|
| `to_utf8()`, `write_utf8(buffer)`, `size_utf8()` | UTF-8, whatever the Type |
| `to_packed()`, `write_packed(buffer)`, `size_packed()` | the Type's own units, packed: UTF-8 bytes for the three char Types, `char16_t` for UTF16, `char32_t` for UTF32 |
| `to_c_string()` | packed with a zero after; nothing when the text holds U+0000 |
| `<<`, `std::format` | UTF-8 |

## Converting

| Call | Gives |
|---|---|
| `jString::convert<To>(text)`, To not `Ascii` | `jString::String<To>`: every Type holds every code point, so it cannot fail |
| `jString::convert<Type::Ascii>(text)` | `std::expected<jString::String<Type::Ascii>, jString::ConversionError>`: the error names the first code point above U+007F and its index |
| `jString::convert_lossy<Type::Ascii>(text)` | `jString::String<Type::Ascii>`, each code point above U+007F made `?` |
| `jString::convert<To, InlineBytes>(text)` | the same, into a String with that much room inside |

Converting between UTF8Packed and UTF8Full packs or spreads; Ascii to UTF8Packed is a copy; every other pair decodes and
encodes each code point.

## Design decisions

| Decision | Why |
|---|---|
| The Type is a required template argument, an enum: `jString::String<jString::Type::UTF16>` | How text is kept decides what is fast. Naming it every time makes it a decision; a default would make it an accident |
| Five Types, not one | Each is the best at something (see "When to use each"); one cannot be best at all of it |
| UTF8Full's cell holds UTF-8 bytes, not the code point as a 32-bit number | Bytes in order read the same on every machine. UTF32 is there for when the number itself is wanted |
| UTF16 and UTF32 in this machine's byte order | They are for handing to APIs on this machine, which expect its order |
| `"text"_js` is text of every Type it fits | One literal for all five, its units made while compiling for each Type it is used as. A literal with é in it does not compile as Ascii |
| A plain `"text"` is not text | Nothing checked it |
| Text from outside always comes in through `String::from()` (or `from_lossy()`) | Each Type takes its own units: bytes, `char16_t` or `char32_t`. Checking and keeping is one pass |
| No conversion between Types without `convert<To>()` | A UTF-16 string compared with a UTF-8 one would be comparing different units |
| Up to 64 bytes held inside a `jString::String` (one cache line), longer text in a `std::vector` | Most short text needs no heap. The place is worked out each time, never kept as a pointer, so a move cannot leave a pointer into the old String. `std::string`'s small-string buffer set off g++ 15's `-Wdangling-pointer`; this cannot |
| `jString::FixedString<T, N>`: at most N code points, all inside, room for the widest | Trivially copyable and destructible, and all zeros is the empty text: a block of them needs no construction, destruction or clearing |
| No `operator[]`; `at()` gives the code point as an `optional` view | Past the end is an empty answer, not a crash or a guess |
| `CodePointIndex` (from 0) and `CodePointNumber` (from 1) are types, and `CodePointCount` is another | A plain number, a base, or a start and a count swapped do not compile |
| Views from a temporary jString do not compile | A view into text that is about to be destroyed is a dangling pointer by another name |
| `to_c_string()` gives nothing when the text holds U+0000 | C would stop at the zero and quietly lose the rest |
| A jString moved from is empty | The standard says only "valid but unspecified" for `std::string`; here it is a promise |
| Header-only C++23: concepts, `std::expected`, `constexpr` | Nothing to build or link, and the compiler checks the rules |

## The types

| Type | What it is |
|---|---|
| `jString::String<T, InlineBytes = 64>` | Owned text: a value type that copies, moves (leaving the source empty), compares and hashes. Up to 64 bytes held inside it, with no heap |
| `jString::FixedString<T, N>` | Owned text of at most N code points, all inside: no heap, no destructor, all zeros is empty. For rows of a store |
| `jString::StringView<T>` | Borrowed text: a pointer and a size, 16 bytes. Keep a `jString::String`, pass a `jString::StringView` |
| `"..."_js` | Text checked while compiling, its units made then for each Type it is used as, which lives as long as the program |
| `jString::Type` | `Ascii`, `UTF8Packed`, `UTF8Full`, `UTF16`, `UTF32` |
| `CodePointPosition<IndexBase>` | A position counted in code points, from 0 (`CodePointIndex`, the default) or from 1 (`CodePointNumber`) |
| `CodePointCount` | A length in code points, so that a start and a length cannot be swapped |
| `IndexBase` | `zero_based` or `one_based`: a scoped enum, never a number |
| `ConversionError` | Why `convert<Type::Ascii>()` failed: the code point and its index |
| `Encoding` | The concept an encoding meets (`Ascii`, `Utf8`, `Utf16`, `Utf32`): a code unit, `scan()`, `encode()`, `decode()` |

## What does not compile

Each of these has a test in `Lin/v001/tests/compile_fail`. Each test also checks that the same file compiles once the
mistake is taken out.

- a jString with no Type: `jString::String<>`
- text of two Types compared, or joined, without `convert<To>()`
- a literal that is not ASCII as Ascii text
- `convert<Type::Ascii>()`'s answer used as a String without checking it
- making a jString from a `char*` or `std::string` without `from()`, or from a view without naming the copy
- a plain `"text"` where text is wanted: it is `"text"_js`
- an invalid UTF-8 literal, or a run-time array passed as a literal
- text turned into a `std::string_view`, implicitly or explicitly, or its units asked for (there is no `units()`)
- `s[0]`
- ignoring `from()`'s answer
- a plain number as a position, a base or a code point; a start and a count swapped
- a view into a temporary jString

## How fast

Timed with `bench/bench_string.cpp` on the host (Core Ultra 7 265, g++ 16, one core, release flags with the
hardening, every VM off). Each Type makes text from its own units. `-`: Ascii cannot hold the mixed text.

| Job | `std::string` | Ascii | UTF8Packed | UTF8Full | UTF16 | UTF32 |
|---|---|---|---|---|---|---|
| 1,024 `at(n)`, 4 KiB mixed | 945 us (walks) | - | 2990 us | 0.83 us | 2100 us | 0.34 us |
| 1,024 substrings of 100 code points, 4 KiB mixed | 977 us | - | 3098 us | 0.42 us | 2207 us | 0.35 us |
| Replace one code point, 4 KiB mixed | 1.05 us | - | 3.9 us | 0.13 us | 2.8 us | 0.13 us |
| Count code points, 4 KiB mixed | 509 ns | - | 356 ns | 0.1 ns | 915 ns | 0.1 ns |
| Make from 4 KiB of ASCII | 45 ns | 280 ns | 284 ns | 632 ns | 432 ns | 633 ns |
| Make from 4 KiB mixed | 37 ns | - | 2.7 us | 5.1 us | 0.66 us | 0.49 us |
| Copy 4 KiB mixed | 48 ns | - | 44 ns | 79 ns | 53 ns | 82 ns |
| Append 18 bytes mixed, 10,000 times | 17.5 us | - | 42 us | 57 us | 49 us | 63 us |
| UTF-8 of 4 KiB mixed, into a buffer | 24 ns (a copy) | - | 24 ns | 2.2 us | 6.7 us | 2.4 us |
| Contains, 4 KiB mixed, not there | 17 ns | - | 17 ns | 49 ns | 517 ns | 504 ns |
| Compare equal, 4 KiB | 26 ns | - | 27 ns | 78 ns | 34 ns | 62 ns |
| Hash, 4 KiB | 389 ns | - | 388 ns | 1187 ns | 605 ns | 1186 ns |
| Kept size of 4 KiB mixed | 4097 B | - | 4097 B | 12300 B | 6320 B | 12300 B |

How to read it:
- **By position** (`at`, `substring`, `replace`, counting), the Types found by multiplying (Ascii, UTF8Full, UTF32)
  are thousands of times faster than walking, whether `std::string` or UTF8Packed and UTF16 do the walking.
- **Passing through** (copy, compare, search, hash, out as UTF-8), UTF8Packed is as fast as `std::string`. UTF8Full
  reads about 3 times the bytes; UTF16 and UTF32 search one unit at a time.
- **Coming in**, every Type checks every unit, which `std::string` never does. UTF16 and UTF32 come in fastest
  from their own units; UTF-8 text with wide code points is the slowest to check.
- A `jString::String` is 96 bytes in every Type (64 held inside); `std::string` is 32.

## Building

From `Lin/v001`: `bash bsir.sh` installs what is missing, then builds and tests with this machine's pinned compilers
(`../../../Compiler_Pins/compilers.pin.jtext`, shared with jCppGUI and jac313):

| Preset | What it does |
|---|---|
| `gcc-debug`, `clang-debug` | Build and run every test, with every warning an error (clang adds `-Weverything`) |
| `gcc-warn` | Optimized, without the hardening flags that hide some warnings: build only |
| `gcc-asan` | AddressSanitizer and UndefinedBehaviorSanitizer |
| `gcc-release`, `clang-release` | Optimized, with the hardening: for reading the benchmark's numbers |

Tested on Fedora 45 (gcc 16, clang 23), RHEL 9 and 10 (gcc-toolset 16, clang 22), and Linux Mint 22 and Zorin 18
(gcc 15, clang 21).
