# Pi-Zero examples

One small app per SCL loader, on the Pi-Zero RP2350B (2x Cortex-M33) under the µOS++ III SMP
kernel. For the full test suite, see `pico2-pizero-shared/` in `SCL-TESTS-shared-new`.

Every app does the same thing, so the loaders can be compared by reading their `main.cpp`:

1. At boot, on core 0, with core 1 still in the bootrom and the kernel not started, load
   `fixtures/floatlib.cpp`.
2. Start SMP.
3. Call the library from core 0, then from core 1, including `float_scale`, which imports
   `app_gain` from main.

| Example | Loader | Library form | Code runs from |
|---|---|---|---|
| `reloc/` | `scl::reloc` | `floatlib.rel.o` | SRAM |
| `static/` | `scl::static` | `floatlib.pic.so` | SRAM |
| `optimal/` | `scl::optimal` | `floatlib.opt.so` | SRAM, at fixed addresses |
| `overlay/` | `scl::overlay` | `ov1.cpp`, `ov2.cpp`, linked with the firmware | one SRAM window |
| `dynamic-newlib/` | `scl::dynamic-newlib` | `floatlib.pic.so` | heap |
| `dynamic-nolib/` | `scl::dynamic-nolib` | `floatlib.so` | SRAM |
| `xip-static/` | `scl::xip-static` | `floatlib.so` | flash |
| `xip-dynamic-nolib/` | `scl::xip-dynamic-nolib` | `floatlib.so` | flash |
| `xip-dynamic-newlib/` | `scl::xip-dynamic-newlib` | `floatlib.so` | flash |

`common/pizero.cpp` holds what they share: boot order, SMP start, the loader lock, reading files
over semihosting and writing flash through the bootrom. Each app only implements `example_load()`.

The `overlay` example swaps only at boot, because a swap is a load. `float_mul` runs once from the
second overlay on core 0; the first overlay then stays resident while both cores call it.

## Run

```sh
./run-all-pizero.sh            # all examples, with a 3-second gap
./run-all-pizero.sh xip        # only those whose name matches
```

The output is the semihosting console. The script does not open the UART, so it stays free for a
terminal, where the firmware prints the same results.

A run passes when the firmware prints `RESULT: PASS`, which requires both cores to get the
expected value from every call.

All 9 passed in one run on the board (2026-10-30), against SCL-shared-new `9f1320f`.

## Requirements

- The port repository (`UOS_PORT_DIR`, default under `/home/dan/Desktop`), for board support and
  `hw.sh`.
- The Pi-Zero's CMSIS-DAP probe (0416:5951), and the xPack toolchain and OpenOCD.

## Not shown here

PSRAM, exceptions, interrupts and missing-import reporting are in the test suite.
