#!/usr/bin/env bash
# Source this to get a shell that can build the firmware.  Every other script sources it;
# there is no other supported way in.  [FIRMWARE.md §1.1]
#
#   . tools/env.sh
#
# Asserts the active IDF is exactly the tag in toolchain.lock -- a stale shell cannot
# silently build against the wrong SDK.

_clk_env_die() { echo "env.sh: $*" >&2; return 1; }

CLK_FW_DIR="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." && pwd)"
CLK_LOCK="$CLK_FW_DIR/toolchain.lock"
[ -f "$CLK_LOCK" ] || { _clk_env_die "no toolchain.lock at $CLK_LOCK"; return 1; }

# Minimal YAML scrape -- the lock file is deliberately flat enough for this.
_clk_lock() { sed -n "s/^  $1: *\"\{0,1\}\([^\"#]*\)\"\{0,1\} *\(#.*\)\{0,1\}$/\1/p" "$CLK_LOCK" | head -1 | sed 's/ *$//'; }

CLK_IDF_TAG="$(_clk_lock tag)"
CLK_IDF_PATH="$(_clk_lock path)"
CLK_PY_VER="$(_clk_lock python)"
CLK_IDF_PATH="${CLK_IDF_PATH/#\~/$HOME}"

[ -n "$CLK_IDF_TAG" ] || { _clk_env_die "could not read idf.tag from toolchain.lock"; return 1; }

# 1. Pin python BEFORE sourcing export.sh (detect_python.sh takes the first PATH hit).
if command -v brew >/dev/null 2>&1; then
    _clk_py_prefix="$(brew --prefix "python@${CLK_PY_VER}" 2>/dev/null || true)"
    if [ -n "$_clk_py_prefix" ] && [ -d "$_clk_py_prefix/libexec/bin" ]; then
        export PATH="$_clk_py_prefix/libexec/bin:$PATH"
    else
        echo "env.sh: WARNING python@${CLK_PY_VER} not installed; IDF will use $(python3 --version 2>&1)" >&2
    fi
fi

# 2. Source the pinned IDF.
if [ ! -d "$CLK_IDF_PATH" ]; then
    _clk_env_die "ESP-IDF $CLK_IDF_TAG not found at $CLK_IDF_PATH -- run tools/idf-setup.sh"
    return 1
fi
# shellcheck disable=SC1091
. "$CLK_IDF_PATH/export.sh" >/dev/null || { _clk_env_die "export.sh failed"; return 1; }

# 3. Assert the tag actually matches what we asked for.
_clk_actual="$(git -C "$CLK_IDF_PATH" describe --tags --exact-match 2>/dev/null \
            || git -C "$CLK_IDF_PATH" describe --tags 2>/dev/null || echo unknown)"
if [ "$_clk_actual" != "$CLK_IDF_TAG" ]; then
    echo "env.sh: WARNING active IDF is '$_clk_actual', toolchain.lock says '$CLK_IDF_TAG'" >&2
fi

export CLK_FW_DIR CLK_IDF_TAG CLK_IDF_PATH
echo "clock fw: IDF $_clk_actual  |  $(python3 --version 2>&1)  |  $CLK_FW_DIR"
