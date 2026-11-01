# Luckfox Lyra B examples

The WeAct examples and test fixtures, on each of the 9 SCL loaders, on the Luckfox Lyra B
(RK3506, 3x Cortex-A7) under the µOS++ III SMP kernel: 67 firmwares.

The board has **no execute-in-place flash that applications may use**: its SPI flash holds the boot
firmware, and every application runs from DRAM. Storage is the **microSD card**, not SPI flash.
For stress tests, see `luckfox-lyra-shared/` in `SCL-TESTS-shared-new`.

All 67 firmwares pass, in one `./run-all-lyra.sh` run at SCL-shared-new `1eafdbe` (2026-11-01).

| App (`apps/`) | Fixtures | What it shows | Loaders |
|---|---|---|---|
| `float` | `floatlib` | Hard-float calls, a constructor at load, library data shared by all three cores | 9 |
| `sd` | `sd` (RK3506 driver) | Init the card, write a pattern and read it back on every core | 9 |
| `combo` | `floatlib`, `sd` | Compute, unload, load the SD library in the same memory, save the result on the card, read it back on all three cores | 9 |
| `cpp` | `cpp3`, `c2`, `c1`, `mylib1` | C++ classes, recursion, heap, float, libraries calling each other in a cycle | 7 (not `static`, `overlay`) |
| `provcons` | `libprov`, `libcons`, `mylib2` | One library importing from another; imports from main | 8 (not `overlay`) |
| `slib` | `mylib1`, `slib` | Constructors, internal calls, an import from main | 8 (not `overlay`) |
| `drivers` | `libled`, `libuart`, `libsd` | The user LED, the debug UART, the SD card | 8 (not `overlay`) |
| `cppeh` | `libcppeh`, `ehx_catch`, `libcppsh` | Exceptions inside a library, across libraries and into main; RTTI; virtual classes | 8 (not `overlay`) |
| `ovfix` | `ov1`, `ov2`, `ovcpp` | The WeAct overlay test's fixtures, swapped at boot | `overlay` only |

| Loader (`loaders/`) | Library form | Code runs from |
|---|---|---|
| `reloc` | `.rel.o` | DRAM arena |
| `static` | `.pic.so` | DRAM arena |
| `optimal` | `.opt.so` | DRAM, at fixed addresses |
| `overlay` | linked with the firmware | one DRAM window |
| `dynamic-newlib` | `.pic.so` | heap |
| `dynamic-nolib` | `.so` | DRAM arena |
| `xip-static` | `.so` | DRAM image region |
| `xip-dynamic-nolib` | `.so` | DRAM image region |
| `xip-dynamic-newlib` | `.so` | DRAM image region |

A firmware is one app plus one loader, for example `combo-xip-static`. Each prints every fixture
call and its traces, on one core and then the next, and checks the values.

## The fixtures

`fixtures/tests/` is a copy of the WeAct tests' fixtures from `SCL-TESTS-shared-new`.
`fixtures/rk3506/` holds the board's driver fixtures: `led.c`, `uart.c` and `sd.c`. The SD fixture
imports `board_sd_store`/`board_sd_load`, which main implements over FatFs on the boot card, under
`tests/sclsd.bin`; nothing else on the card is touched.

`fixtures/floatlib.cpp` is the hard-float library, and `fixtures/ovfloat.cpp` is its overlay form.

## Boot order

1. `app_load()`: on core 0, with the other cores parked and the kernel not started.
2. `app_start()`: on core 0, with the kernel running. The SD card comes up here, because its
   driver needs the kernel's clock.
3. `app_on_core()`: on core 0, then 1, then 2, with SMP running.
4. `app_together()`: on all cores at once. None of these apps needs it.

## The user LED

Every firmware uses the board's user LED (GPIO1_A0) as a status light, so a run can be read
without a console:

- two blinks when the firmware comes up;
- one blink per core as that core starts its walkthrough (one for core 0, two for core 1,
  three for core 2);
- steady on for PASS, and a burst of ten blinks for FAIL.

`blink_led(times)` is in `common/lyra.cpp`; the `drivers` app additionally exercises the libled
fixture, which blinks five times on each core. After a run the LED stays on (PASS) until `hw.sh`
resets the board for the next one.

## "XIP" without flash

The `xip-*` loaders patch the image and copy it into a static DRAM region (64 KiB per slot), then
run it in place from there with its data in RAM. That is the same code path as the Lyra test suite;
only the address is DRAM rather than flash. `write_to_flash()` in `common/lyra.cpp` is that copy,
and it cleans the data cache and invalidates the instruction cache.

## Differences from the Pi-Zero examples

- There is no SPI flash: `spifs` is replaced by `sd`, and `flashsmp` (writing flash with both cores
  running) has no meaning here, so it is not ported.
- `spi` is replaced by `sd` in `drivers`.
- Three cores, not two, so `app_on_core()` runs three times and the `float` call counter reaches 6.
- `libsd` is linked from `micro-os-plus::devices-rk3506`; `board_sd.cpp` supplies the strong
  `board_sd_store`/`board_sd_load` only for the SD apps.
- `ovfloat.cpp` has no `calls` counter, so `float-overlay` does not check it.

## Run

```sh
./run-all-lyra.sh            # all 67, with a 3-second gap
./run-all-lyra.sh xip        # only those whose name matches
```

The output is the semihosting console, over the WCH-Link CMSIS-DAP probe. `hw.sh` does a full
RK3506 global reset before each run. The script does not open the UART, so it stays free for a
terminal, where the firmware prints the same results.

A run passes when the firmware prints `RESULT: PASS`, which requires every value checked on all
three cores to be right.

`luckfox-lyra/` pins SCL-shared-new `749da4a`; the run above was at `1eafdbe`, before the
ARM A32 loader support. `common-elf-reader` is the `elf32_read_parse-new.git` submodule at
`f650889`.

## Requirements

- The aarch32 port repository (`UOS_PORT_DIR`, default under `/home/dan/Desktop`), for board support
  and `hw.sh`.
- A WCH-Link CMSIS-DAP probe (1a86:8011), and the xPack toolchain and OpenOCD.
- An SD card in the board's slot, with its boot FAT partition.

## Not shown here

Interrupts, missing-import reporting and calls from both cores at once under load are in the test
suite. With exceptions, `cppeh` throws on one core at a time.
