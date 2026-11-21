# File: bsir.sh
# Purpose: Bootstrap, Setup, Install and Run for jString Lin/v001: install what is missing, build, test.
#
#   1. Stop unless this is Fedora or Red Hat Enterprise Linux (dnf), or Linux Mint or Zorin OS (apt and lsb_release).
#   2. Check each component with its `check` line in Setup/recipes.conf, this OS's own line where it has one.
#      Red Hat Enterprise Linux uses Fedora's lines where it has none of its own.
#   3. Run the system update on every run, without asking.
#   4. Offer anything missing, once, and install it from Setup/recipes.conf.
#   5. Offer what is optional the same way (jString has nothing optional yet).
#   6. Build and test with one compiler and then the other, never at the same time, with this machine's pinned compilers
#      (<base>/Compiler_Pins/compilers.pin.jtext, shared with jCppGUI, jac313, Qlite, jText and jText_GUI). A machine with no
#      pin is pinned to this OS's compilers from Setup/recipes.conf (Linux Mint and Zorin OS: g++-15 and clang++-21; Red Hat
#      Enterprise Linux: the newest GCC Toolset and the newest Clang, from Setup/newest_compilers.sh).
#   7. Check the source rules (tests/check_source_rules.sh).
#
# Run it as: bash bsir.sh. Everything is found from this script's own directory. Re-run it any time.
set -eu

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"
RECIPES="$ROOT/Setup/recipes.conf"
COMPONENTS="gcc clang cmake ninja"
OPTIONAL_COMPONENTS=""

ASSUME_YES=0
for arg in "$@"; do
  case "$arg" in
    -y|--yes) ASSUME_YES=1 ;;
    --no-demo) ;;  # jString has no demo. Accepted so that a runner can start every project the same way.
    -h|--help)
      echo "usage: bash bsir.sh [--yes] [--no-demo]"
      echo "  --yes      install everything that is missing, optional things too, without asking"
      echo "  --no-demo  accepted, and changes nothing: jString has no demo"
      exit 0 ;;
    *) echo "unknown argument: $arg (try --help)" >&2; exit 2 ;;
  esac
done

# --- 1. Fedora, Red Hat Enterprise Linux, Linux Mint or Zorin OS ---
OS=""
if [ -n "$(command -v dnf)" ] && [ -n "$(rpm --eval '%{?rhel}')" ]; then
  OS=rhel
elif [ -n "$(command -v dnf)" ]; then
  OS=fedora
elif [ -n "$(command -v apt-get)" ] && [ -n "$(command -v lsb_release)" ] && [ "$(lsb_release -si)" = "Linuxmint" ]; then
  OS=linuxmint
elif [ -n "$(command -v apt-get)" ] && [ -n "$(command -v lsb_release)" ] && [ "$(lsb_release -si)" = "Zorin" ]; then
  # Zorin OS is its own OS, with its own lines in Setup/recipes.conf: named by name, never taken as Mint or Ubuntu.
  OS=zorin
fi
if [ -z "$OS" ]; then
  echo "This script supports Fedora, Red Hat Enterprise Linux, Linux Mint and Zorin OS only, and this system is none of them. Nothing was changed." >&2
  echo "To build by hand, see README.md." >&2
  exit 1
fi
echo "System: $OS"

# A recipe value, joining backslash-continued lines: recipe <component> <field>
# This OS's own <component>.<field>.<os> comes first, then on rhel fedora's, then the shared <component>.<field>.
recipe() {
  value="$(recipe_key "$1.$2.$OS")"
  [ -n "$value" ] || [ "$OS" != rhel ] || value="$(recipe_key "$1.$2.fedora")"
  [ -n "$value" ] || value="$(recipe_key "$1.$2")"
  printf '%s\n' "$value"
}

recipe_key() {
  awk -v full="$1" '
    function is_key(line,   eq, key) {
      eq = index(line, "=")
      if (eq == 0) return 0
      key = substr(line, 1, eq - 1); gsub(/^[ \t]+|[ \t]+$/, "", key)
      return key == full
    }
    !found && is_key($0) { found = 1; sub(/^[^=]*=[ \t]*/, "") }
    found {
      more = ($0 ~ /\\[ \t]*$/)
      sub(/\\[ \t]*$/, ""); sub(/[ \t]+$/, "")
      out = out (out == "" ? "" : " ") $0
      if (!more) { print out; exit }
    }
  ' "$RECIPES"
}

present() {
  check="$(recipe "$1" check)"
  [ -n "$check" ] && output="$(sh -c "$check" 2>&1)"
}

# Those of the given components that are not here, in $missing.
find_missing() {
  missing=""
  for component in "$@"; do
    present "$component" || missing="$missing $component"
  done
}

# What goes without each optional component. Nothing but a warning: the build does not need them.
warn_optional() {
  for component in $1; do
    case "$component" in
      meld) echo "Warning: Meld is not installed. Diff will use the built-in viewer, for two files only." >&2 ;;
      *) echo "Warning: $component is not installed." >&2 ;;
    esac
  done
}

# --- 2. what is here ---
find_missing $COMPONENTS $OPTIONAL_COMPONENTS
echo "Missing:${missing:- nothing}"

# --- 3. the system update runs on every run, without asking ---
echo
echo "=== system update ==="
sh -c "$(recipe system update)" || echo "  [FAIL] system update; going on"

# --- 4. offer anything missing, once ---
find_missing $COMPONENTS
if [ -n "$missing" ]; then
  if [ "$ASSUME_YES" = 0 ]; then
    if [ ! -t 0 ]; then
      echo "Missing:$missing. Run bash bsir.sh in a terminal, or bash bsir.sh --yes" >&2
      exit 1
    fi
    printf "Install%s? [Y/n] " "$missing"
    read -r answer || answer=n
    case "$answer" in
      [nN]*) echo "Not installing. The build cannot go on without them." >&2; exit 1 ;;
    esac
  fi
  for component in $missing; do
    echo
    echo "=== $(recipe "$component" desc) ==="
    sh -c "$(recipe "$component" install)" || echo "  [FAIL] (see above)"
  done
  find_missing $COMPONENTS
  if [ -n "$missing" ]; then
    echo "Still missing:$missing. Fix the errors above, then re-run bash bsir.sh" >&2
    exit 1
  fi
fi

# --- 5. offer what is optional, once, and go on whatever becomes of it ---
find_missing $OPTIONAL_COMPONENTS
if [ -n "$missing" ]; then
  answer=y
  if [ "$ASSUME_YES" = 0 ]; then
    if [ -t 0 ]; then
      printf "Install%s, which is optional? [Y/n] " "$missing"
      read -r answer || answer=n
    else
      answer=n
    fi
  fi
  case "$answer" in
    [nN]*) ;;
    *)
      for component in $missing; do
        echo
        echo "=== $(recipe "$component" desc) ==="
        sh -c "$(recipe "$component" install)" || echo "  [FAIL] (see above)"
      done
      find_missing $OPTIONAL_COMPONENTS ;;
  esac
  warn_optional "$missing"
fi

# --- 6. build and test, one compiler at a time ---
# Every preset builds with this machine's pinned compilers (Setup/pinned_compilers.cmake reads them), so a preset run by
# hand builds with the same ones as this script. The pin is one row per machine (uname -n) in
# <base>/Compiler_Pins/compilers.pin.jtext, beside the repository, or the file $COMPILER_PINS names: the same file
# jCppGUI, jac313, Qlite, jText and jText_GUI build with. A row already there is kept, whoever wrote it. A machine with none is
# pinned here to this OS's compilers from Setup/recipes.conf, written the way jac313 writes the file.
PIN_FILE="${COMPILER_PINS:-$(realpath -m "$ROOT/../../../Compiler_Pins/compilers.pin.jtext")}"
PIN_HOST="$(uname -n)"

# This OS's C++ compiler of family $1 (gcc or clang) from Setup/recipes.conf, as a path. A `compilers` command, where
# there is one, prints the C and then the C++ compiler.
recipe_cxx() {
  command="$(recipe "$1" compilers)"
  if [ -n "$command" ]; then
    cxx="$(sh -c "$command" | sed -n 2p)"
  else
    cxx="$(recipe "$1" cxx)"
  fi
  case "$cxx" in
    */*) printf '%s\n' "$cxx" ;;
    *) command -v "$cxx" ;;
  esac
}

# This machine's pinned C++ compiler of family $1, from its row: id|host|gcc_version|gcc_path|clang_version|clang_path|pinned.
pinned_cxx() {
  [ -f "$PIN_FILE" ] || return 0
  awk -v host="$PIN_HOST" -v field="$([ "$1" = gcc ] && echo 4 || echo 6)" '
    sub(/^ *1\. #\|# /, "") { split($0, f, "|"); if (f[2] == host) { print f[field]; exit } }' "$PIN_FILE"
}

pin_this_machine() {
  gcc="$(recipe_cxx gcc)" && [ -n "$gcc" ] || { echo "No g++ to pin: see Setup/recipes.conf." >&2; exit 1; }
  clang="$(recipe_cxx clang)" && [ -n "$clang" ] || { echo "No clang++ to pin: see Setup/recipes.conf." >&2; exit 1; }
  row="$PIN_HOST|$("$gcc" -dumpfullversion)|$gcc|$("$clang" -dumpversion)|$clang|$(date +%F)"
  case "$row" in *"
"*) echo "A compiler pin value holds a line break: $row" >&2; exit 1 ;; esac
  mkdir -p "$(dirname "$PIN_FILE")"
  if [ ! -f "$PIN_FILE" ]; then
    # Exactly the header jac313 and tools/hooks/compiler-pin.sh write, so any of them can rewrite the file.
    printf '%s\n' \
      "//File:    compilers.pin.jtext" \
      "//Purpose: The GCC and Clang each machine builds with, pinned. Outside every repository (like" \
      "//         SQL_Secrets), so removing the git folder starts over. One row per machine (hostname)." \
      "//         Written by tools/hooks/compiler-pin.sh (Qlite, jText, jText_GUI). Values never contain '|'." \
      "//" "=== jText File ===" " 1. #?# compilers.pin" " 3. #?# Pinned compilers per machine" "" "" \
      "=== Section: pins ===" "" "" "=== Fields ===" \
      " 1. #/# id/Identity(1,1)/Not Null" \
      " 2. #/# host/String/Not Null          # uname -n" \
      " 3. #/# gcc_version/String/Not Null   # when pinned; the major version is what is held" \
      " 4. #/# gcc_path/String/Not Null" \
      " 5. #/# clang_version/String/Not Null" \
      " 6. #/# clang_path/String/Not Null" \
      " 7. #/# pinned/String/Not Null        # date of the last pin or upgrade" \
      "=== End Fields ===" "" "" "=== Data ===" "=== End Data ===" "=== End Section ===" "=== End File ===" \
      > "$PIN_FILE.tmp"
    mv "$PIN_FILE.tmp" "$PIN_FILE"
  fi
  # The new row goes last in the data, numbered after the highest id. Written beside the file, then renamed over it.
  awk -v row="$row" '
    { line[NR] = $0 }
    /^ *1\. #\|# / { s = $0; sub(/^ *1\. #\|# /, "", s); split(s, f, "|"); if (f[1] + 0 > top) top = f[1] + 0 }
    END {
      for (i = 1; i <= NR; i++) {
        if (line[i] == "=== End Data ===") { print "1. #|# " (top + 1) "|" row; print "" }
        print line[i]
      }
    }' "$PIN_FILE" > "$PIN_FILE.tmp"
  grep -q "^1\. #|# [0-9]*|$PIN_HOST|" "$PIN_FILE.tmp" || { echo "Could not add this machine to $PIN_FILE." >&2; rm -f "$PIN_FILE.tmp"; exit 1; }
  mv "$PIN_FILE.tmp" "$PIN_FILE"
  echo "Pinned this machine, $PIN_HOST, to $gcc and $clang in $PIN_FILE."
}

echo
echo "=== compiler pin ==="
[ -n "$(pinned_cxx gcc)" ] || pin_this_machine
echo "$PIN_HOST builds with $(pinned_cxx gcc) and $(pinned_cxx clang), pinned in $PIN_FILE."

# A program's real file: a name is looked for on PATH, and links are followed.
real_file() {
  case "$1" in
    */*) readlink -f "$1" ;;
    *) found="$(command -v "$1")" && readlink -f "$found" ;;
  esac
}

# A build directory keeps the compiler it was configured with, and CMakeLists.txt refuses one that is no longer the pin,
# as after a newer GCC Toolset is pinned. Remove it first, so that it is configured again from nothing.
drop_if_compiler_changed() {
  cache="build/$1/CMakeCache.txt"
  [ -f "$cache" ] || return 0
  was="$(sed -n 's/^CMAKE_CXX_COMPILER:[^=]*=//p' "$cache" | head -n 1)"
  now="$(pinned_cxx "${1%%-*}")"
  [ "$(real_file "$was")" = "$(real_file "$now")" ] && return 0
  echo "Removing build/$1: it was configured with $was, and this machine is pinned to $now."
  rm -rf "build/$1"
}

for preset in gcc-debug clang-debug; do
  echo
  echo "=== $preset ==="
  drop_if_compiler_changed "$preset"
  cmake --preset "$preset"
  cmake --build --preset "$preset"
  ctest --preset "$preset"
done

# Build only: optimized, without the flags that stop some warnings firing. See docs/SECURITY.md.
echo
echo "=== gcc-warn ==="
drop_if_compiler_changed gcc-warn
cmake --preset gcc-warn
cmake --build --preset gcc-warn


# --- 7. the source rules ---
echo
echo "=== source rules ==="
bash tests/check_source_rules.sh

echo
echo "Bootstrap complete."
