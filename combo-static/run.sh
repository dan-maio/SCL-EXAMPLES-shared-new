#!/usr/bin/env bash
# run.sh — spifs-static (XIP + r10). Build fixture (r10 PIC .so) + firmware, flash
# the WeAct F411CE, run with semihosting (spifs.so read over semihosting into the
# on-chip flat-fs then loaded XIP; the on-board W25Q64 is driven by the loaded
# module; output on USART1 PA9 115200 AND the semihosting console).
#   ./run.sh              # build + flash + run (Ctrl-C to stop)
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
echo "== 1) fixture: spifs.so (W25Q64 driver + flatfs, r10 PIC split) =="
make -C "$HERE/fixtures" TC="$TC"
echo "== 2) configure + build firmware (WeAct) =="
[ -d "$BUILD" ] || cmake -S "$HERE" -B "$BUILD" \
    -DCMAKE_TOOLCHAIN_FILE="$PLATFORM/cmake/arm-none-eabi.cmake" -DSCL_PLATFORM=board -DSCL_BOARD=weact
cmake --build "$BUILD" --target spifs-static -j
echo "== 3) flash + run (semihosting; fs output also on UART PA9 115200) =="
killall -s 9 openocd 2>/dev/null || true
CMD=( "$OPENOCD" -s "$OPENOCD_SCRIPTS" -f "$HERE/openocd-weact.cfg"
      -c "init" -c "reset halt" -c "program $BUILD/spifs-static.hex"
      -c "arm semihosting enable" -c "reset run" )
if [[ -n "${RUN_MS:-}" ]]; then CMD+=( -c "sleep $RUN_MS" -c "shutdown" ); fi
exec "${CMD[@]}"
