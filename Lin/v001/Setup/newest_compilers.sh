# File: Setup/newest_compilers.sh
# Purpose: The newest GCC and Clang on Red Hat Enterprise Linux. Both are found the same way: the search starts at a
#          first version and counts down, and the first one this system offers is the one, so a newer one is found
#          without a change here. Only the newest is needed; an older one that is installed is left alone.
#            gcc    The system g++ stays at the release default, and Red Hat ships each newer GCC beside it as
#                   gcc-toolset-N: RHEL 10.3 has gcc-toolset-16. How a toolset is run differs by release, so each
#                   release has its own way here (toolset_run), and a release without one stops with a message:
#                     RHEL 10  gcc-toolset-N-env <command>
#                     RHEL 9   scl enable gcc-toolset-N -- <command> (RHEL 9 has no gcc-toolset-N-env); installed
#                              when scl lists gcc-toolset-N and its g++ is there
#            clang  The plain clang package is updated with each release, and older ones stay beside it as clangN:
#                   RHEL 10.3 has clang 22, and clang15 to clang21. Red Hat's clang is set to build with one fixed
#                   GCC Toolset's C++ library (gcc-toolset-15 on RHEL 10.3), so it is given the newest one's instead,
#                   which keeps the two compilers on the same library.
#
#   bash Setup/newest_compilers.sh --check gcc|clang      succeed when the newest offered is installed
#   bash Setup/newest_compilers.sh --install gcc|clang    install it
#   bash Setup/newest_compilers.sh --compilers gcc|clang  print the C and then the C++ compiler, one per line
#   bash Setup/newest_compilers.sh --cxx-flags clang      print the flag that gives clang the newest GCC Toolset's library
#
# Setup/recipes.conf runs it for gcc and clang on rhel.
set -u

readonly FIRST_GCC_TO_TRY=20
readonly LAST_GCC_TO_TRY=14
readonly FIRST_CLANG_TO_TRY=30
readonly LAST_CLANG_TO_TRY=19

# Every package the enabled repositories offer, and every one installed, whose name matches: "name major" per line.
# dnf is asked once, and the searches below look through its answer.
# dnf never waits for an answer here. A repository whose signing key was never imported makes dnf ask
# "Is this ok [y/N]" to import it; the question lands in the captured output, where no one sees it, and dnf
# waits for ever. --assumeno answers no without reading the terminal, and that repository is skipped
# instead of failing the whole query.
known_packages() {
  {
    dnf -q --assumeno --setopt=skip_if_unavailable=True repoquery --qf '%{name} %{version}\n' "$@" 2>&1
    rpm -qa --qf '%{NAME} %{VERSION}\n' "$@"
  } | sed -n 's/^\([^ ]*\) \([0-9]*\)\..*$/\1 \2/p'
}

# The newest GCC Toolset offered or installed: its N.
newest_gcc_offered() {
  known="$(known_packages 'gcc-toolset-*-gcc-c++')"
  n=$FIRST_GCC_TO_TRY
  while [ "$n" -ge "$LAST_GCC_TO_TRY" ]; do
    if printf '%s\n' "$known" | grep -q "^gcc-toolset-$n-gcc-c++ "; then
      echo "$n"
      return 0
    fi
    n=$((n - 1))
  done
  return 1
}

# This Red Hat Enterprise Linux release's major version: 9, 10, ...
readonly RHEL_MAJOR="$(rpm --eval '%{?rhel}')"

# Run a command inside GCC Toolset $1, the way this release runs a toolset: toolset_run <N> <command> [args...]
toolset_run() {
  n="$1"
  shift
  case "$RHEL_MAJOR" in
    10) "gcc-toolset-$n-env" "$@" ;;
    9) scl enable "gcc-toolset-$n" -- "$@" ;;
    *) echo "no way to run a GCC Toolset is known for Red Hat Enterprise Linux '$RHEL_MAJOR'" >&2; return 1 ;;
  esac
}

# Whether GCC Toolset $1 is installed, the way this release installs a toolset.
toolset_installed() {
  case "$RHEL_MAJOR" in
    10) [ -n "$(command -v "gcc-toolset-$1-env")" ] ;;
    9) scl list-collections 2>&1 | grep -qx "gcc-toolset-$1" && [ -n "$(scl enable "gcc-toolset-$1" -- sh -c 'command -v g++')" ] ;;
    *) return 1 ;;
  esac
}

# The newest GCC Toolset installed: its N.
newest_gcc_installed() {
  n=$FIRST_GCC_TO_TRY
  while [ "$n" -ge "$LAST_GCC_TO_TRY" ]; do
    if toolset_installed "$n"; then
      echo "$n"
      return 0
    fi
    n=$((n - 1))
  done
  return 1
}

# The newest Clang offered or installed, as "major package": the plain clang where that is the one, else clangN.
newest_clang_offered() {
  known="$(known_packages clang 'clang[0-9]*')"
  n=$FIRST_CLANG_TO_TRY
  while [ "$n" -ge "$LAST_CLANG_TO_TRY" ]; do
    if printf '%s\n' "$known" | grep -qx "clang $n"; then
      echo "$n clang"
      return 0
    fi
    if printf '%s\n' "$known" | grep -qx "clang$n $n"; then
      echo "$n clang$n"
      return 0
    fi
    n=$((n - 1))
  done
  return 1
}

# The C and C++ commands of Clang major $1, when installed: clang-N, else the plain clang when it is that major.
clang_commands() {
  if [ -n "$(command -v "clang++-$1")" ] && [ -n "$(command -v "clang-$1")" ]; then
    printf '%s\n%s\n' "clang-$1" "clang++-$1"
    return 0
  fi
  [ -n "$(command -v clang++)" ] || return 1
  v="$(clang++ -dumpversion | cut -d. -f1)"
  [ "$v" = "$1" ] || return 1
  printf '%s\n%s\n' clang clang++
}

# The newest installed Clang's major: the highest clang-N, or the plain clang's.
newest_clang_installed() {
  n=$FIRST_CLANG_TO_TRY
  while [ "$n" -ge "$LAST_CLANG_TO_TRY" ]; do
    if clang_commands "$n" > "$work/commands"; then
      echo "$n"
      return 0
    fi
    n=$((n - 1))
  done
  return 1
}

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

case "${1:-} ${2:-}" in
  "--check gcc")
    offered="$(newest_gcc_offered)" || { echo "no gcc-toolset is offered or installed" >&2; exit 1; }
    [ "$(newest_gcc_installed)" = "$offered" ] ;;
  "--install gcc")
    offered="$(newest_gcc_offered)" || { echo "no gcc-toolset is offered by the enabled repositories" >&2; exit 1; }
    echo "Installing gcc-toolset-$offered, the newest this system offers"
    sudo dnf install -y "gcc-toolset-$offered" "gcc-toolset-$offered-gcc-c++" ;;
  "--compilers gcc")
    installed="$(newest_gcc_installed)" || { echo "no gcc-toolset is installed" >&2; exit 1; }
    toolset_run "$installed" sh -c 'command -v gcc && command -v g++' ;;
  "--check clang")
    offered="$(newest_clang_offered)" || { echo "no clang is offered or installed" >&2; exit 1; }
    [ "$(newest_clang_installed)" = "${offered%% *}" ] ;;
  "--install clang")
    offered="$(newest_clang_offered)" || { echo "no clang is offered by the enabled repositories" >&2; exit 1; }
    package="${offered#* }"
    echo "Installing $package ${offered%% *}, the newest Clang this system offers, and its clang-query"
    sudo dnf install -y "$package" "$package-tools-extra" ;;
  "--compilers clang")
    installed="$(newest_clang_installed)" || { echo "no clang is installed" >&2; exit 1; }
    clang_commands "$installed" ;;
  "--cxx-flags clang")
    installed="$(newest_gcc_installed)" || { echo "no gcc-toolset is installed" >&2; exit 1; }
    library="$(toolset_run "$installed" g++ -print-libgcc-file-name)"
    echo "--gcc-install-dir=${library%/*}" ;;
  *)
    echo "usage: bash Setup/newest_compilers.sh --check|--install|--compilers gcc|clang, or --cxx-flags clang" >&2
    exit 2 ;;
esac
