#!/usr/bin/env bash
# Install the ESP-IDF pinned in toolchain.lock.  Idempotent -- safe to re-run.
# [FIRMWARE.md §1.1]
set -euo pipefail

FW_DIR="$(cd "$(dirname "$0")/.." && pwd)"
LOCK="$FW_DIR/toolchain.lock"
lock() { sed -n "s/^  $1: *\"\{0,1\}\([^\"#]*\)\"\{0,1\} *\(#.*\)\{0,1\}$/\1/p" "$LOCK" | head -1 | sed 's/ *$//'; }

TAG="$(lock tag)"
IDF_DIR="$(lock path)"; IDF_DIR="${IDF_DIR/#\~/$HOME}"
TARGETS="$(lock targets)"
PY_VER="$(lock python)"

echo "==> ESP-IDF $TAG  ->  $IDF_DIR   (targets: $TARGETS)"

# Pin python before install.sh runs: detect_python.sh ignores $ESP_PYTHON and takes the
# first of python3/python/python3.9..3.13 on PATH.
if command -v brew >/dev/null 2>&1; then
    PY_PREFIX="$(brew --prefix "python@${PY_VER}" 2>/dev/null || true)"
    if [ -z "$PY_PREFIX" ]; then
        echo "==> installing python@${PY_VER} (IDF $TAG is not tested against newer)"
        brew install "python@${PY_VER}"
        PY_PREFIX="$(brew --prefix "python@${PY_VER}")"
    fi
    export PATH="$PY_PREFIX/libexec/bin:$PATH"
fi
echo "==> python: $(python3 --version 2>&1) ($(command -v python3))"

if [ -d "$IDF_DIR/.git" ]; then
    have="$(git -C "$IDF_DIR" describe --tags 2>/dev/null || echo unknown)"
    if [ "$have" != "$TAG" ]; then
        echo "!!  $IDF_DIR holds '$have', not '$TAG'." >&2
        echo "!!  Refusing to mutate it -- point idf.path at a new directory instead." >&2
        exit 1
    fi
    echo "==> already cloned at $TAG"
else
    mkdir -p "$(dirname "$IDF_DIR")"
    git clone -b "$TAG" --depth 1 --shallow-submodules --recursive \
        https://github.com/espressif/esp-idf.git "$IDF_DIR"
fi

"$IDF_DIR/install.sh" "$TARGETS"

cat <<EOF

==> done.  Start every session with:

      . $FW_DIR/tools/env.sh

    then:  tools/build.sh dev devkit flash monitor
EOF
