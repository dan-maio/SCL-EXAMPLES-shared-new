# Pi-Zero examples

The WeAct examples and test fixtures, on each of the 9 SCL loaders, on the Pi-Zero RP2350B (2x
Cortex-M33) under the µOS++ III SMP kernel: 76 firmwares. For stress tests and PSRAM, see
`pico2-pizero-shared/` in `SCL-TESTS-shared-new`.

| App (`apps/`) | Fixtures | What it shows | Loaders |
|---|---|---|---|
| `float` | `floatlib` | Hard-float calls, a constructor at load, library data shared by both cores | 9 |
| `spifs` | flat filesystem + driver | Format, create, list, read and delete by name; both cores list and read | 9 |
| `combo` | `floatlib`, `spifs` | Compute, unload, load the filesystem in the same memory, save the result, read it back on both cores | 9 |
| `flashsmp` | `spifs` | Format and write files with both cores running | 9 |
| `cpp` | `cpp3`, `c2`, `c1`, `mylib1` | C++ classes, recursion, heap, float, libraries calling each other in a cycle | 7 (not `static`, `overlay`) |
| `provcons` | `libprov`, `libcons`, `mylib2` | One library importing from another; imports from main | 8 (not `overlay`) |
| `slib` | `mylib1`, `slib` | Constructors, internal calls, an import from main | 8 (not `overlay`) |
| `drivers` | `libled`, `libuart`, `libspi` | The LED, the UART, the SPI block in internal loopback | 8 (not `overlay`) |
| `cppeh` | `libcppeh`, `ehx_catch`, `libcppsh` | Exceptions inside a library, across libraries and into main; RTTI; virtual classes | 8 (not `overlay`) |
| `ovfix` | `ov1`, `ov2`, `ovcpp` | The WeAct overlay test's fixtures, swapped at boot | `overlay` only |

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

A firmware is one app plus one loader, for example `combo-xip-static`. Each prints every fixture
call and its traces, on one core and then the other, and checks the values.

## The fixtures

`fixtures/tests/` is a copy of the WeAct tests' fixtures from `SCL-TESTS-shared-new`, with the RP2350
driver fixtures from `pico2-pizero-shared/`.

For `float`, `spifs` and `combo`, the build compiles the WeAct examples' files directly: `floatlib.cpp`, `spi-shim.cpp`,
`fs-archive-spi.cpp` and `spifs-api.cpp` from `../combo-reloc/fixtures/`, and `ovfloat.cpp` from
`../combo-overlay/fixtures/`.

Only `w25q64.cpp` is replaced, because the board has neither a W25Q64 nor the STM32 HAL.
`fixtures/w25q64-rp2350.cpp` is the same class over 1 MB of the board's own QSPI flash, starting at
9 MB. Each `spifs` and `combo` run erases that 1 MB.

The chip is a Puya (`85 20 18`) and `spifs_init()` accepts only Winbond's `0xEF`, so the driver
reports a good ID as `0xEF`. The firmware prints the real ID.

## Boot order

1. `app_load()`: on core 0, with core 1 in the bootrom and the kernel not started.
2. `app_start()`: on core 0, with the kernel running and core 1 still in the bootrom.
3. `app_on_core()`: on core 0, then on core 1, with SMP running.
4. `app_together()`: on both cores at once. Only `flashsmp` uses it.

## Writing flash with both cores running

`flashsmp` uses `scl::rp2350-flash` from the loader; `include/scl/rp2350-flash.h` has its rules. The
writer forces a TIMER1 interrupt on the other core, which waits in SRAM, using a vector table in
SRAM, until the write ends. A sector erase stops both cores' interrupts for tens of milliseconds, so
this does not suit hard real-time work.

- The filesystem has no lock of its own: `flashsmp` serialises calls into it.

The other apps use it too, but write only before core 1 starts, so nothing is parked.

Differences from the WeAct examples:

- `ovfloat.cpp` has no `calls` counter, so `float-overlay` does not check it.
- The overlay builds drop the shim's empty `w25q64()` constructor from the startup table, because the
  window is empty when constructors run.

## Run

```sh
./run-all-pizero.sh            # all 76, with a 3-second gap
./run-all-pizero.sh spifs      # only those whose name matches
```

The output is the semihosting console. The script does not open the UART, so it stays free for a
terminal, where the firmware prints the same results.

A run passes when the firmware prints `RESULT: PASS`, which requires every value checked on both
cores to be right.

`pizero/` pins SCL-shared-new `86c83d2`. All 76 passed in one full run on the board (2026-10-31).

`Failed semihosting operation` can appear in a passing run's log. Its cause is not known.

## Requirements

- The port repository (`UOS_PORT_DIR`, default under `/home/dan/Desktop`), for board support and
  `hw.sh`.
- The Pi-Zero's CMSIS-DAP probe (0416:5951), and the xPack toolchain and OpenOCD.

## Not shown here

PSRAM, interrupts, missing-import reporting and calls from both cores at once under load are in the
test suite. With exceptions, `cppeh` throws on one core at a time.
