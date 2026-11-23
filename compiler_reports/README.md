# Compiler reports

jCppGUI and jString (repository jac331) build with every warning an error, on both g++ and clang++, with no
warning turned off to hide a false one. Doing that found three compiler problems, and one thing clang could add. Each is cut down here to one small file, checked on the
compiler named, with the command that shows it.

| # | Compiler | Problem |
|---|---|---|
| 1 | clang++ 23.1 | `-Wlifetime-safety-invalidation` reports a reference parameter invalidated by `push_back` |
| 2 | clang++ 23.1 | Asks for `[[clang::lifetimebound]]`, then cannot verify it, wherever it is put |
| 3 | g++ 16.2 | Rejects `[[clang::lifetimebound]]`, and so misses a dangling view that clang++ catches |
| 4 | clang++ 23.1 | Feature request: a way to mark one `default:` as deliberate, for `-Wcovered-switch-default` |

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

## 2. clang++: asks for [[clang::lifetimebound]], then cannot verify it

**Compiler:** clang version 23.1.5 (Fedora 23.1.5-2.fc45)

Save as `clang_2_suggest_then_cannot_verify.cpp`. `LB` is the attribute on the declaration, `LB2` on the definition:

```cpp
#include <tuple>

template <class Node>
constexpr auto& find(Node& node LB);

template <class Tuple>
constexpr auto& find_in(Tuple& children) { return find(std::get<0>(children)); }

template <class Node>
constexpr auto& find(Node& node LB2) {
    if constexpr (requires { node.children; }) return find_in(node.children);
    else return node;
}

struct Leaf { int value = 0; };
struct Branch { std::tuple<Leaf> children; };

int main() {
    Branch b;
    return find(b).value;
}
```

```
F="-std=c++23 -Wlifetime-safety -Wlifetime-safety-suggestions -Wlifetime-safety-lifetimebound-violation -fsyntax-only"
clang++ $F -DLB= -DLB2= clang_2_suggest_then_cannot_verify.cpp
clang++ $F "-DLB=[[clang::lifetimebound]]" -DLB2= clang_2_suggest_then_cannot_verify.cpp
clang++ $F -DLB= "-DLB2=[[clang::lifetimebound]]" clang_2_suggest_then_cannot_verify.cpp
clang++ $F "-DLB=[[clang::lifetimebound]]" "-DLB2=[[clang::lifetimebound]]" clang_2_suggest_then_cannot_verify.cpp
```

- Without the attribute: `warning: parameter in intra-TU function should be marked [[clang::lifetimebound]] [-Wlifetime-safety-intra-tu-suggestions]`
- With it on the declaration, the definition, or both: `warning: could not verify that the return value can be lifetime bound to 'node' [-Wlifetime-safety-lifetimebound-violation]`

**Expected:** one or the other. The function returns `node` itself or, through `find_in`, a reference to a member
of a member of `node`, so the attribute is correct and should verify. As it is, no version of this code builds
clean with both groups on. The call through a forward-declared template seems to be what the verifier cannot
follow.

A second case, not yet cut down: a class template's constructor, `Button(CheckedText caption, OnClick on_click)`,
that moves `on_click` (a lambda's type) into a member gets the same suggest-then-cannot-verify pair.

**What we tested:** with every `[[clang::lifetimebound]]` clang++ 23.1 asked for added to jCppGUI (121, in a
scratch copy), clang++ checked every caller and found no dangling anywhere; these two places are all that stood
between it and a clean build.

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
on both compilers cannot use the attribute at all without a macro or `-Wno-attributes=clang::lifetimebound`, so
clang++'s check is lost on all code that must also build with g++.

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
