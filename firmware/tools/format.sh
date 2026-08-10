#!/usr/bin/env bash
# tools/format.sh [--check] [file...]        clang-format the C++ tree.  [firmware/.clang-format]
#
#   tools/format.sh                 # rewrite every C++ file under firmware/
#   tools/format.sh --check         # exit 1 and list the files that are not formatted (CI/hook)
#   tools/format.sh a.cpp b.hpp     # just these
#
# Style lives in firmware/.clang-format, the binary version in toolchain.lock (§1.1) -- the
# major matters, clang-format's output drifts between them and an unpinned tool turns every
# commit into a reformat war.
set -euo pipefail

FW_DIR="$(cd "$(dirname "$0")/.." && pwd)"
LOCK="$FW_DIR/toolchain.lock"

# --- find a clang-format ------------------------------------------------------------------
# $CLANG_FORMAT wins; then Homebrew LLVM (not on PATH by default -- it is keg-only); then
# whatever is on PATH; then the Xcode toolchain, which every mac has.
find_clang_format() {
    local c
    for c in "${CLANG_FORMAT:-}" \
             "$(brew --prefix llvm 2>/dev/null)/bin/clang-format" \
             "$(command -v clang-format 2>/dev/null || true)" \
             "$(xcrun -f clang-format 2>/dev/null || true)"; do
        [ -n "$c" ] && [ -x "$c" ] && { echo "$c"; return 0; }
    done
    return 1
}

CF="$(find_clang_format || true)"
if [ -z "$CF" ]; then
    echo "format.sh: no clang-format found." >&2
    echo "           brew install llvm      (or set \$CLANG_FORMAT to one)" >&2
    exit 127
fi

# Warn, never fail, on a version skew: a wrong-major run still formats, it just may disagree
# with the last one, and failing here would block a commit over a tool nobody asked to bump.
WANT="$(sed -n 's/^  clang-format: *"\{0,1\}\([0-9]*\).*/\1/p' "$LOCK" | head -1)"
HAVE="$("$CF" --version | sed -n 's/.*version \([0-9]*\).*/\1/p' | head -1)"
if [ -n "$WANT" ] && [ -n "$HAVE" ] && [ "$WANT" != "$HAVE" ]; then
    echo "format.sh: WARNING clang-format $HAVE ($CF), toolchain.lock pins $WANT" >&2
fi

# --- what to format -----------------------------------------------------------------------
CHECK=0
[ "${1:-}" = "--check" ] && { CHECK=1; shift; }

files=()
if [ $# -gt 0 ]; then
    files=("$@")
else
    # build/ is generated, vendor/ is other people's code -- neither is ours to restyle.
    while IFS= read -r f; do files+=("$f"); done < <(
        find "$FW_DIR" \
            \( -path "$FW_DIR/build" -o -path "$FW_DIR/vendor" -o -name .git \) -prune -o \
            \( -name '*.cpp' -o -name '*.cc' -o -name '*.hpp' -o -name '*.h' -o -name '*.c' \) \
            -print | sort
    )
fi
[ ${#files[@]} -eq 0 ] && { echo "format.sh: nothing to format"; exit 0; }

if [ "$CHECK" -eq 1 ]; then
    bad=()
    for f in "${files[@]}"; do
        "$CF" --style=file --output-replacements-xml "$f" | grep -q "<replacement " && bad+=("$f")
    done
    if [ ${#bad[@]} -gt 0 ]; then
        printf 'not formatted: %s\n' "${bad[@]}" >&2
        echo "run: firmware/tools/format.sh" >&2
        exit 1
    fi
    echo "format: ${#files[@]} files clean  (clang-format $HAVE)"
else
    "$CF" --style=file -i "${files[@]}"
    echo "format: ${#files[@]} files  (clang-format $HAVE)"
fi
