#!/usr/bin/env bash
# run.sh — float-static-full-ram (PURE static loader, module full in RAM (float)).
# Build the plain -fPIC floatlib.so + firmware, flash the WeAct F411CE, run with
# semihosting (floatlib.so is read over semihosting WHOLE into a RAM buffer and run
# there; output on USART1
# PA9 115200 AND the semihosting console).
#   ./run.sh              # build + flash + run
#   RUN_MS=60000 ./run.sh
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCL="$HERE/SCL-shared"
PLATFORM="$HERE/platform"
GIT_REMOTE="${GIT_REMOTE:-/home/dan/Downloads/GIT}"
# loader provided by the shared SCL-shared clone (symlinked into $SCL); no clone needed
TC="${ARM_TOOLCHAIN_BIN:-/home/dan/.local/xPacks/@xpack-dev-tools/arm-none-eabi-gcc/15.2.1-1.1.1/.content/bin}"
export PATH="$TC:$PATH"
OPENOCD="${OPENOCD:-$(command -v openocd || echo /home/dan/.local/xPacks/@xpack-dev-tools/openocd/0.12.0-7.1/.content/bin/openocd)}"
OPENOCD_SCRIPTS="${OPENOCD_SCRIPTS:-/home/dan/.local/xPacks/@xpack-dev-tools/openocd/0.12.0-7.1/.content/openocd/scripts}"
BUILD="$HERE/build"
echo "== 1) fixture: floatlib.so (plain -fPIC ET_DYN) =="
make -C "$HERE/fixtures" TC="$TC"
echo "== 2) configure + build firmware (WeAct) =="
[ -d "$BUILD" ] || cmake -S "$HERE" -B "$BUILD" -DCMAKE_TOOLCHAIN_FILE="$PLATFORM/cmake/arm-none-eabi.cmake"
cmake --build "$BUILD" --target float-static-full-ram -j
echo "== 3) flash + run (semihosting; fs output also on UART PA9 115200) =="
killall -s 9 openocd 2>/dev/null || true
CMD=( "$OPENOCD" -s "$OPENOCD_SCRIPTS" -f "$HERE/openocd-weact.cfg"
      -c "init" -c "reset halt" -c "program $BUILD/float-static-full-ram.hex"
      -c "arm semihosting enable" -c "reset run" )
if [[ -n "${RUN_MS:-}" ]]; then CMD+=( -c "sleep $RUN_MS" -c "shutdown" ); fi
exec "${CMD[@]}"
