# Pi-Zero examples

The WeAct `float`, `spifs` and `combo` examples, for each of the 9 SCL loaders, on the Pi-Zero
RP2350B (2x Cortex-M33) under the µOS++ III SMP kernel: 27 firmwares. For the full test suite, see
`pico2-pizero-shared/` in `SCL-TESTS-shared-new`.

| App (`apps/`) | What it does |
|---|---|
| `float` | Loads `floatlib`, calls it from both cores, and checks its `calls` counter |
| `spifs` | Loads the flat filesystem, then formats, creates, lists, reads and deletes files; both cores then list and read |
| `combo` | Computes with `floatlib`, saves the results to a file with `spifs`; both cores read it back |

| Loader (`loaders/`) | Library form | Code runs from |
|---|---|---|
| `reloc` | `.rel.o` | SRAM |
| `static` | `.pic.so` | SRAM |
| `optimal` | `.opt.so` | SRAM, at fixed addresses |
| `overlay` | linked with the firmware | one SRAM window |
| `dynamic-newlib` | `.pic.so` | heap |
| `dynamic-nolib` | `.so` | SRAM |
| `xip-static` | `.so` | flash |
| `xip-dynamic-nolib` | `.so` | flash |
| `xip-dynamic-newlib` | `.so` | flash |

A firmware is one app plus one loader, for example `combo-xip-static`.

## The fixtures

The build compiles the WeAct examples' files directly: `floatlib.cpp`, `spi-shim.cpp`,
`fs-archive-spi.cpp` and `spifs-api.cpp` from `../combo-reloc/fixtures/`, and `ovfloat.cpp` from
`../combo-overlay/fixtures/`.

Only `w25q64.cpp` is replaced, because the board has neither a W25Q64 nor the STM32 HAL.
`fixtures/w25q64-rp2350.cpp` is the same class over 1 MB of the board's own QSPI flash, starting at
9 MB. Each `spifs` and `combo` run erases that 1 MB.

The chip is a Puya (`85 20 18`) and `spifs_init()` accepts only Winbond's `0xEF`, so the driver
reports a good ID as `0xEF`. The firmware prints the real ID.

## Boot order

1. `app_load()`: on core 0, with core 1 in the bootrom and the kernel not started.
2. `app_start()`: on core 0, with the kernel running and core 1 still in the bootrom. Flash writes
   happen here, because writing stops execute-in-place and core 1 must not be running from flash.
3. `app_on_core()`: on core 0, then on core 1, with SMP running. Reads only.

Differences from the WeAct examples:

- `combo` keeps both libraries loaded, except with `overlay`, where the second load overwrites the
  first, as on the WeAct.
- `ovfloat.cpp` has no `calls` counter, so `float-overlay` does not check it.
- The overlay builds drop the shim's empty `w25q64()` constructor from the startup table, because the
  window is empty when constructors run.

## Run

```sh
./run-all-pizero.sh            # all 27, with a 3-second gap
./run-all-pizero.sh spifs      # only those whose name matches
```

The output is the semihosting console. The script does not open the UART, so it stays free for a
terminal, where the firmware prints the same results.

A run passes when the firmware prints `RESULT: PASS`, which requires every value checked on both
cores to be right.

All 27 passed on the board (2026-10-30) against SCL-shared-new `9f1320f`, over several runs rather
than one, because the run was stopped and restarted while fixing the overlay startup and the chip ID.

## Requirements

- The port repository (`UOS_PORT_DIR`, default under `/home/dan/Desktop`), for board support and
  `hw.sh`.
- The Pi-Zero's CMSIS-DAP probe (0416:5951), and the xPack toolchain and OpenOCD.

## Not shown here

PSRAM, exceptions, interrupts, missing-import reporting and the larger test fixtures are in the
test suite.
