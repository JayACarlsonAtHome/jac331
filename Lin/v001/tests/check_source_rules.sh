# File: tests/check_source_rules.sh
# Purpose: jString's source rules, by text. Comments and the insides of strings are removed first, so prose cannot trip
#          them. Run it as: bash tests/check_source_rules.sh (bsir.sh and ctest both do).
#            - no #pragma but #pragma once: a warning is fixed, never silenced
#            - no #define: names and constants are C++, not macros (tests' JSTR_BREAK is the one #ifdef, set from outside)
#            - no reinterpret_cast, const_cast, new or delete: storage is std::vector's, and units are char
#            - no std::string in include/: jString is what replaces it
set -u -o pipefail
VERSION_DIR="$(cd "$(dirname "$0")/.." && pwd)" || exit 2
cd "$VERSION_DIR" || exit 2

failures=0
report() {  # report <rule> <what was found>
  if [ -n "$2" ]; then
    echo "RULE BROKEN: $1"
    echo "$2"
    failures=$((failures + 1))
  fi
}

# Each file with its comments and string and character literals emptied, as "file:line:text".
stripped() {
  for file in "$@"; do
    sed -E -e 's://.*$::' -e "s/'([^'\\\\]|\\\\.)*'/''/g" -e 's/"([^"\\]|\\.)*"/""/g' "$file" | awk -v f="$file" '{ print f ":" NR ":" $0 }'
  done
}

mapfile -t sources < <(find include tests -type f \( -name '*.hpp' -o -name '*.cpp' \) | sort)
[ "${#sources[@]}" -gt 0 ] || { echo "no sources found under $VERSION_DIR"; exit 2; }
code="$(stripped "${sources[@]}")"
headers="$(printf '%s\n' "$code" | grep '^include/')"

report "a #pragma other than #pragma once" "$(printf '%s\n' "$code" | grep -E ':[0-9]+:[[:space:]]*#[[:space:]]*pragma' | grep -vE '#[[:space:]]*pragma[[:space:]]+once[[:space:]]*$')"
report "a #define" "$(printf '%s\n' "$code" | grep -E ':[0-9]+:[[:space:]]*#[[:space:]]*define\b')"
report "an #ifdef other than JSTR_BREAK in a compile-fail case" "$(printf '%s\n' "$code" | grep -E ':[0-9]+:[[:space:]]*#[[:space:]]*(ifdef|ifndef|if)\b' | grep -vE '^tests/compile_fail/[a-z_]+\.cpp:[0-9]+:#ifdef JSTR_BREAK$')"
report "reinterpret_cast or const_cast" "$(printf '%s\n' "$code" | grep -E '\b(reinterpret_cast|const_cast)\b')"
report "a new or delete expression" "$(printf '%s\n' "$code" | grep -E '(^|[^_[:alnum:]])(new|delete)[[:space:]]*(\[|\(|[[:alpha:]_])' | grep -vE '= *delete;')"
report "std::string (not std::string_view) in include/" "$(printf '%s\n' "$headers" | grep -E 'std::(basic_)?string\b([^_]|$)')"

if [ "$failures" -gt 0 ]; then
  echo "$failures source rule(s) broken"
  exit 1
fi
echo "source rules hold (${#sources[@]} files)"
