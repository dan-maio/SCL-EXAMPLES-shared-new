#!/usr/bin/env bash
# run.sh — float-dynamic (POSIX dlopen, RAM / scl::dynamic-newlib). Build the
# plain -fPIC fixture + firmware, flash the WeAct F411CE, run with semihosting
# (floatlib.so is read over semihosting by the dl:: facade; results on the
# semihosting console).
#   ./run.sh                 # build + flash + run (Ctrl-C to stop)
#   RUN_MS=10000 ./run.sh    # auto-shutdown after 10 s
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCL="$HERE/dynamic-shared"
# loader provided by the in-example SCL-shared submodule (symlinked into $SCL); no clone needed
TC="${ARM_TOOLCHAIN_BIN:-/home/dan/.local/xPacks/@xpack-dev-tools/arm-none-eabi-gcc/15.2.1-1.1.1/.content/bin}"
export PATH="$TC:$PATH"
OPENOCD="${OPENOCD:-$(command -v openocd || echo /home/dan/.local/xPacks/@xpack-dev-tools/openocd/0.12.0-7.1/.content/bin/openocd)}"
OPENOCD_SCRIPTS="${OPENOCD_SCRIPTS:-/home/dan/.local/xPacks/@xpack-dev-tools/openocd/0.12.0-7.1/.content/openocd/scripts}"
BUILD="$HERE/build"

echo "== 1) fixture: floatlib.so (plain -fPIC ET_DYN) =="
make -C "$HERE/fixtures" TC="$TC"
echo "== 2) configure + build firmware (WeAct, hosted) =="
[ -d "$BUILD" ] || cmake -S "$HERE" -B "$BUILD" \
    -DCMAKE_TOOLCHAIN_FILE="$SCL/cmake/arm-none-eabi-hosted.cmake" -DSCL_PLATFORM=board -DSCL_BOARD=weact
cmake --build "$BUILD" --target float-dynamic -j
echo "== 3) flash + run (semihosting) =="
killall -s 9 openocd 2>/dev/null || true
CMD=( "$OPENOCD" -s "$OPENOCD_SCRIPTS" -f "$HERE/openocd-weact.cfg"
      -c "init" -c "reset halt" -c "program $BUILD/float-dynamic.hex"
      -c "arm semihosting enable" -c "reset run" )
if [[ -n "${RUN_MS:-}" ]]; then CMD+=( -c "sleep $RUN_MS" -c "shutdown" ); fi
exec "${CMD[@]}"
