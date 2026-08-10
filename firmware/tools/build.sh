#!/usr/bin/env bash
# tools/build.sh [PROFILE] [BOARD] [idf.py args...]      [FIRMWARE.md §1.2]
#
#   tools/build.sh                          -> dev / devkit / build
#   tools/build.sh dev devkit flash monitor
#   tools/build.sh release rev0_3 build
#   tools/build.sh dev devkit-uart flash monitor    (UART console, for boot crashes)
#
# PROFILE and BOARD expand to an sdkconfig fragment list; sdkconfig itself is git-ignored
# so a hand-edited menuconfig can never be committed.
set -euo pipefail

FW_DIR="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck disable=SC1091
. "$FW_DIR/tools/env.sh"

PROFILE="${1:-dev}";  [ $# -gt 0 ] && shift || true
BOARD="${1:-devkit}"; [ $# -gt 0 ] && shift || true
ACTION=("$@"); [ ${#ACTION[@]} -eq 0 ] && ACTION=(build)

case "$PROFILE" in dev|release) ;; *) echo "PROFILE must be dev|release" >&2; exit 1;; esac

APP="$FW_DIR/apps/clock"
FRAGMENTS="sdkconfig.defaults;sdkconfig.$PROFILE"

case "$BOARD" in
    devkit)      FRAGMENTS="$FRAGMENTS;sdkconfig.devkit" ;;
    devkit-uart) FRAGMENTS="$FRAGMENTS;sdkconfig.devkit;sdkconfig.devkit-uart" ;;
    rev0_3)      FRAGMENTS="$FRAGMENTS;sdkconfig.rev0_3" ;;
    *) echo "BOARD must be devkit|devkit-uart|rev0_3" >&2; exit 1 ;;
esac

echo "==> PROFILE=$PROFILE BOARD=$BOARD"
echo "==> fragments: $FRAGMENTS"

exec idf.py -C "$APP" \
    -B "$FW_DIR/build/$PROFILE-$BOARD" \
    -D SDKCONFIG_DEFAULTS="$FRAGMENTS" \
    -D SDKCONFIG="$FW_DIR/build/$PROFILE-$BOARD/sdkconfig" \
    -D CLK_PROFILE="$PROFILE" -D CLK_BOARD="$BOARD" \
    "${ACTION[@]}"
