#!/usr/bin/env bash
# run.sh — spifs-reloc (RAM/ET_REL). Build the SPI-flatfs fixture + firmware,
# flash the WeAct F411CE, run with semihosting (spifs.rel.o is read over
# semihosting; the on-board W25Q64 is driven by the loaded module; output on
# USART1 PA9 115200 AND the semihosting console).
#   ./run.sh              # build + flash + run (Ctrl-C to stop)
#   RUN_MS=60000 ./run.sh
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCL="$HERE/relocatable-shared"
GIT_REMOTE="${GIT_REMOTE:-/home/dan/Downloads/GIT}"
# loader provided by the shared SCL-shared clone (symlinked into $SCL); no clone needed
# relocatable-shared ships no STM32 HAL; copy it from the reference repo (once)
REF="${REF_REPO:-/home/dan/Documents/shared-loaders-cortex-m-qemu}"
if [ ! -d "$HERE/vendor/stm32f4-hal" ]; then
  V="$REF/test/platforms/board-hosted/vendor"
  mkdir -p "$HERE/vendor/stm32f4-hal" "$HERE/vendor/cmsis" "$HERE/vendor/include"
  cp "$V"/common/system/src/stm32f4-hal/*.c "$HERE/vendor/stm32f4-hal/"
  cp "$V"/common/system/src/cmsis/system_stm32f4xx.c "$HERE/vendor/cmsis/"
  cp -r "$V"/common/system/include/. "$HERE/vendor/include/"
  cp "$V"/common/firmware/include/stm32f4xx_hal_conf.h "$HERE/vendor/include/"
fi
TC="${ARM_TOOLCHAIN_BIN:-/home/dan/.local/xPacks/@xpack-dev-tools/arm-none-eabi-gcc/15.2.1-1.1.1/.content/bin}"
export PATH="$TC:$PATH"
OPENOCD="${OPENOCD:-$(command -v openocd || echo /home/dan/.local/xPacks/@xpack-dev-tools/openocd/0.12.0-7.1/.content/bin/openocd)}"
OPENOCD_SCRIPTS="${OPENOCD_SCRIPTS:-/home/dan/.local/xPacks/@xpack-dev-tools/openocd/0.12.0-7.1/.content/openocd/scripts}"
BUILD="$HERE/build"
echo "== 1) fixture: spifs.rel.o (W25Q64 driver + flatfs, ET_REL) =="
make -C "$HERE/fixtures" TC="$TC"
echo "== 2) configure + build firmware (WeAct) =="
[ -d "$BUILD" ] || cmake -S "$HERE" -B "$BUILD" \
    -DCMAKE_TOOLCHAIN_FILE="$SCL/cmake/arm-none-eabi.cmake" -DSCL_PLATFORM=board -DSCL_BOARD=weact
cmake --build "$BUILD" --target spifs-reloc -j
echo "== 3) flash + run (semihosting; fs output also on UART PA9 115200) =="
killall -s 9 openocd 2>/dev/null || true
CMD=( "$OPENOCD" -s "$OPENOCD_SCRIPTS" -f "$HERE/openocd-weact.cfg"
      -c "init" -c "reset halt" -c "program $BUILD/spifs-reloc.hex"
      -c "arm semihosting enable" -c "reset run" )
if [[ -n "${RUN_MS:-}" ]]; then CMD+=( -c "sleep $RUN_MS" -c "shutdown" ); fi
exec "${CMD[@]}"
