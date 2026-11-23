# Compiler reports

jCppGUI and jString (repository jac331) build with every warning an error, on both g++ and clang++, with no
warning turned off to hide a false one. Doing that found four compiler problems, and one thing clang could add. Each is cut down here to one small file, checked on the
compiler named, with the command that shows it.

| # | Compiler | Problem |
|---|---|---|
| 1 | clang++ 23.1 | `-Wlifetime-safety-invalidation` reports a reference parameter invalidated by `push_back` |
| 2 | clang++ 23.1, libstdc++ 16 | A reference returned through `std::get` cannot be marked `[[clang::lifetimebound]]` and verified |
| 3 | g++ 16.2 | Rejects `[[clang::lifetimebound]]`, and so misses a dangling view that clang++ catches |
| 4 | clang++ 23.1 | Feature request: a way to mark one `default:` as deliberate, for `-Wcovered-switch-default` |
| 5 | clang++ 23.1 | One template parameter: `[[clang::lifetimebound]]` asked for by one instantiation, unverifiable in another |

Trackers: clang at https://github.com/llvm/llvm-project/issues, gcc at https://gcc.gnu.org/bugzilla/.

---

## 1. clang++: -Wlifetime-safety-invalidation reports a reference parameter invalidated by push_back

**Compiler:** clang version 23.1.5 (Fedora 23.1.5-2.fc45)

Save as `clang_1_invalidation.cpp`:

```cpp
#include <vector>

void append_two(std::vector<int>& out) {
    out.push_back(1);
    out.push_back(2);  // reported: parameter 'out' is later invalidated
}

int main() {
    std::vector<int> v;
    append_two(v);
    return static_cast<int>(v.size()) - 2;
}
```

`clang++ -std=c++23 -Wlifetime-safety -fsyntax-only clang_1_invalidation.cpp` gives:

```
clang_1_invalidation.cpp:3:17: warning: parameter 'out' is later invalidated [-Wlifetime-safety-invalidation]
clang_1_invalidation.cpp:4:9: note: parameter 'out' is invalidated here
clang_1_invalidation.cpp:5:5: note: later used here
```

**Expected:** no warning. `push_back` can invalidate pointers, references and iterators to the vector's
*elements*, but `out` refers to the vector object itself, which `push_back` never invalidates. The same
pattern on a local vector is not reported.

**Why it matters:** appending to an out-parameter more than once is one of the most common ways to build output
in C++. In jCppGUI it fired at 9 places, all false. The only ways to a clean build were to turn off the whole
invalidation check, which also hides the real bugs it exists to find, or to rewrite each function to build into
a local and append once. We rewrote them.

---

## 2. clang++ and libstdc++: a reference returned through std::get cannot be lifetimebound

**Compiler:** clang version 23.1.5 (Fedora 23.1.5-2.fc45), with libstdc++ from GCC 16.2.1

Save as `clang_2_get_not_lifetimebound.cpp`:

```cpp
#include <tuple>

struct Leaf { int value = 0; };
struct Pair { Leaf first; Leaf second; };

// The same reference into the same argument, once through std::get and once by member access.
const Leaf& through_get(const std::tuple<Leaf, Leaf>& t LB) { return std::get<0>(t); }
const Leaf& through_member(const Pair& p LB) { return p.first; }

int main() {
    const std::tuple<Leaf, Leaf> t{};
    const Pair p{};
    return through_get(t).value + through_member(p).value;
}
```

```
F="-std=c++23 -Wlifetime-safety -Wlifetime-safety-suggestions -Wlifetime-safety-lifetimebound-violation -fsyntax-only"
clang++ $F -DLB= clang_2_get_not_lifetimebound.cpp
clang++ $F "-DLB=[[clang::lifetimebound]]" clang_2_get_not_lifetimebound.cpp
```

- Without the attribute: `8:41: warning: parameter in intra-TU function should be marked [[clang::lifetimebound]] [-Wlifetime-safety-intra-tu-suggestions]`,
  for `through_member` only. Nothing is said about `through_get`.
- With it: `7:57: warning: could not verify that the return value can be lifetime bound to 't' [-Wlifetime-safety-lifetimebound-violation]`,
  for `through_get` only. `through_member` verifies.

**Expected:** the two functions do the same thing, and both should verify. libstdc++'s `std::get` (and `<tuple>` as
a whole) carries no `[[clang::lifetimebound]]`, so clang++ cannot see that `std::get<0>(t)` refers into `t`, and every
function that returns a reference through it can neither be marked nor be left unmarked under `-Werror`: the
suggestion is missed, and the attribute, once written, cannot be verified.

**Asking for:** libstdc++ to mark `std::get` for `tuple`, `pair`, `array` and `variant` (and `std::get_if`) with
`[[clang::lifetimebound]]` when the compiler is clang++, so that clang++'s lifetime analysis can follow it. (libc++ was
not tested here.)

**What we did:** jCppGUI walked a widget's children with `std::get`, and every `[[clang::lifetimebound]]` above it in
the walk could not be verified: 1166 warnings, from two lines, one per instantiation. The children are now plain
structs reached by member access, and with every `[[clang::lifetimebound]]` clang++ 23.1 asked for written (145 in
jCppGUI, 14 in jString), clang++ verifies all of them and finds no dangling anywhere.

An earlier version of this report blamed a forward-declared template. That was wrong: the same forward declaration
verifies once `std::get` is out of the path.

---

## 3. g++: [[clang::lifetimebound]] is rejected, and the dangling it would catch is missed

**Compiler:** g++ (GCC) 16.2.1 20261029 (Red Hat 16.2.1-3), with clang version 23.1.5 for comparison

Save as `gcc_lifetimebound.cpp`:

```cpp
#include <string>
#include <string_view>

std::string_view view_of(const std::string& text [[clang::lifetimebound]]) { return text; }

int main() {
    const std::string_view v = view_of(std::string{"text"});  // v dangles: the string is gone at the semicolon
    return static_cast<int>(v.size()) - 4;
}
```

`g++ -std=c++23 -Wall -Wextra -fsyntax-only gcc_lifetimebound.cpp` gives only:

```
warning: 'clang::lifetimebound' scoped attribute directive ignored [-Wattributes]
```

and nothing about the dangling view. `clang++ -std=c++23 -fsyntax-only gcc_lifetimebound.cpp` gives:

```
warning: temporary whose address is used as value of local variable 'v' will be destroyed at the end of the full-expression [-Wdangling]
```

**Asking for:** g++ support for `lifetimebound`, as `[[gnu::lifetimebound]]` or by accepting
`[[clang::lifetimebound]]`, so that `-Wdangling-reference` can use it. Today a project that builds with `-Werror`
on both compilers cannot use the attribute at all without a macro or `-Wno-attributes=clang::lifetimebound`.
jCppGUI and jString now pass that flag to g++: g++ accepts the attribute and still rejects any other or misspelled
`clang::` one, but checks nothing with it, so on g++ the dangling view above still goes unreported.

---

## 4. clang++ feature request: mark a default label as deliberate

**Compiler:** clang version 23.1.5 (Fedora 23.1.5-2.fc45); g++ (GCC) 16.2.1 for the second half

Save as `clang_4_deliberate_default.cpp`:

```cpp
#include <cstdint>
#include <cstdio>

enum class Reason : std::uint8_t { FirstFrame, Resize, Damage };

// Every value is named (-Wswitch-enum), and the default is there for a value outside the enum.
const char* name_of(Reason reason) {
    switch (reason) {
        case Reason::FirstFrame: return "FirstFrame";
        case Reason::Resize: return "Resize";
        case Reason::Damage: return "Damage";
        default: return "Unknown";  // reported by -Wcovered-switch-default
    }
}

int main() {
    // A byte read from a file or a socket: any value its type can hold.
    const auto from_outside = static_cast<Reason>(7);
    std::puts(name_of(from_outside));
    return 0;
}
```

`clang++ -std=c++23 -Wswitch-enum -Wswitch-default -Wcovered-switch-default -fsyntax-only clang_4_deliberate_default.cpp` gives:

```
clang_4_deliberate_default.cpp:12:9: warning: default label in switch which covers all enumeration values [-Wcovered-switch-default]
```

and with the `default:` removed, `-Wswitch-default` gives `'switch' missing 'default' label`. Built and run, the program
prints `Unknown`: the default is reached, by a value the enum does not name.

**The conflict:** `-Wswitch-enum` (name every value) with `-Wswitch-default` (always have a default) is a sound pair. The
first catches a value added to the enum and not handled, even when a default is present; the second gives a value from
outside the enum a chosen place to go. `-Wcovered-switch-default` exists to catch the first case for code without
`-Wswitch-enum`, so with it the pair cannot be kept: every switch over an enum is an error one way or the other.
`-Weverything` turns all three on, and there is no way to say, at one switch, that its default is meant.

**What exists:** `[[clang::enum_extensibility(open)]]` on the enum does stop the warning, and `-Wswitch-enum` still
reports a missing value:

```cpp
enum class [[clang::enum_extensibility(open)]] Reason : std::uint8_t { FirstFrame, Resize, Damage };
```

But it is a statement about the enum, made once for every switch over it, and g++ 16.2 does not know it:
`g++ -std=c++23 -Wattributes` gives `'clang::enum_extensibility' scoped attribute directive ignored [-Wattributes]`, an
error under `-Werror`. Code that builds with both compilers and every warning an error cannot use it.

**Requested:** an attribute on the label, in the way `[[fallthrough]]` marks one deliberate fall-through:

```cpp
[[clang::deliberate]] default: return "Unknown";  // or any spelling: "this default is for values outside the enum"
```

that stops `-Wcovered-switch-default` at that switch only, and ideally a matching proposal for a standard attribute so
that g++ would accept it too. Until then jCppGUI and jString keep `-Wswitch-enum` and `-Wswitch-default` and turn off
`-Wcovered-switch-default`, with the reason beside the flag.

---

## 5. clang++: one template parameter, lifetimebound asked for by one instantiation and unverifiable in another

**Compiler:** clang version 23.1.5 (Fedora 23.1.5-2.fc45)

Save as `clang_5_template_attribute_shared.cpp`:

```cpp
#include <utility>

template <class OnClick>
class Button {
public:
    constexpr explicit Button(OnClick on_click LB) : on_click_{std::move(on_click)} {}
    constexpr void click() { on_click_(); }

private:
    OnClick on_click_;
};

int main() {
    int clicks = 0;
    Button counts{[&clicks] { ++clicks; }};  // keeps a reference: the attribute is asked for
    Button quiet{[] {}};                     // keeps none: the attribute cannot be verified
    counts.click();
    quiet.click();
    return clicks - 1;
}
```

```
F="-std=c++23 -Wlifetime-safety -Wlifetime-safety-suggestions -Wlifetime-safety-lifetimebound-violation -fsyntax-only"
clang++ $F -DLB= clang_5_template_attribute_shared.cpp
clang++ $F "-DLB=[[clang::lifetimebound]]" clang_5_template_attribute_shared.cpp
```

- Without the attribute: `6:47: warning: parameter in intra-TU constructor should be marked [[clang::lifetimebound]] [-Wlifetime-safety-intra-tu-constructor-suggestions]`
- With it: `6:48: warning: could not verify that the return value can be lifetime bound to 'on_click' [-Wlifetime-safety-lifetimebound-violation]`

With only `counts`, the attribute verifies. With only `quiet`, nothing is asked for.

**Expected:** an attribute on a template's parameter is one attribute for every instantiation, so the suggestion and
the check should agree across them: either not report an instantiation whose type keeps no reference (there is
nothing for the attribute to bind, so it is not wrong there), or not suggest the attribute unless every instantiation
in the translation unit can carry it. As it is, a template used with both kinds of type cannot build clean either way.

**What we did:** jCppGUI's `Button` takes its `on_click` by `OnClick&&` and by `const OnClick&` instead of by value.
For a reference parameter clang++ asks for nothing, and every button builds clean.

