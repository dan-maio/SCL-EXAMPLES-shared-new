# How the examples grow, from simple to complex

This repository's examples form a ladder. Each step adds one kind of difficulty to the step before
it, so a failure at one step points at what that step added. This guide walks the ladder in order.

| Step | Examples | What it adds | Board |
|---|---|---|---|
| 1 | `float-*` | Load a library and call it | WeAct |
| 2 | `spifs-*`, `spi-static-full-ram` | A real driver that imports from main and uses hardware | WeAct |
| 3 | `combo-*` | Two libraries in turn, with results kept across the swap | WeAct |
| 4 | `api-*` | The new symbol-table APIs, and checks that they were really used | WeAct |
| 5 | `pizero/` `float`, `spifs`, `combo` | The same fixtures on two cores, under an RTOS | Pi-Zero |
| 6 | `pizero/` `slib`, `provcons`, `cpp`, `ovfix` | Libraries that import from each other, C++, overlays | Pi-Zero |
| 7 | `pizero/` `drivers` | Libraries that drive the chip's peripherals themselves | Pi-Zero |
| 8 | `pizero/` `cppeh` | C++ exceptions across library boundaries | Pi-Zero |
| 9 | `pizero/` `flashsmp` | Writing flash with both cores running | Pi-Zero |

Steps 1 to 4 are in folders at the top of the repository, one folder per example: 25 in all. Steps 5
to 9 are in `pizero/`, where one build makes all 76 firmwares.

## The two axes

Every step can be read along two axes:

- **What the library does**, which grows as you go down the table above.
- **How it is loaded**, which is the loader. The same fixture is built once per loader.

| Loader | Library form | Code runs from | Loader code at call time |
|---|---|---|---|
| `scl::reloc` | ET_REL object | RAM | None |
| `scl::static` | plain `-fPIC` `.so` | RAM | None |
| `scl::optimal` | `-fno-pic`, fixed addresses | fixed window | None |
| `scl::overlay` | linked with the firmware | one shared window | None |
| `scl::dynamic-newlib` | plain `-fPIC` `.so` | heap | None |
| `scl::dynamic-nolib` | r10 `.so` | RAM | Lookup, for calls into main or another library |
| `scl::xip-static` | r10 `.so` | flash | Lookup, for calls into another library |
| `scl::xip-dynamic-nolib` | r10 `.so` | flash | Lookup, for calls into main or another library |
| `scl::xip-dynamic-newlib` | r10 `.so` | flash | Lookup, for calls into main or another library |

The top folders name their loader differently from `pizero/`. For example, `float-static` there uses
`scl::xip-static`, and `float-static-full-ram` uses `scl::static`. In `pizero/`, the name is always the
loader's: `float-xip-static` and `float-static`.

## Step 1: `float-*`, the smallest library

**What it is for:** the smallest possible use of each loader. If a new setup can't run this, nothing
else will work.

**The library** is one file, `fixtures/src/floatlib.cpp`, with no imports:

```cpp
extern "C" unsigned calls = 0;                 // library data, exported
namespace { struct Banner { Banner() noexcept { calls = 0; } }; Banner g_banner; }

extern "C" float float_add(float a, float b) { ++calls; return a + b; }
extern "C" float float_mul(float a, float b) { ++calls; return a * b; }
```

**What the app does:**

1. Read the library file from the PC over semihosting. `float-overlay` doesn't: its code is linked
   into the firmware.
2. Hand it to the loader, which places and relocates it and runs its constructor.
3. Get the functions, either by name or as direct calls, depending on the loader.
4. Call them, and print the results and the counter.

**Output** of `float-reloc` on the WeAct:

```text
float_add(2.500, 3.500)= 6.000
float_mul(2.500, 3.500)= 8.750
floatlib calls = 2
float-reloc: done
```

**What a correct run proves:**

- The values show that hard-float arguments and results pass through correctly.
- `calls = 2` shows that the library's data is writable and in the right place.
- A constructor ran at load, and a data symbol can be found by name, not only functions.

**What changes per loader** is the part to compare:

| Example | Loader | How main calls it |
|---|---|---|
| `float-reloc` | `scl::reloc` | Pointers from `reloc::sym()` |
| `float-static-full-ram` | `scl::static` | Direct calls, after main's GOT is patched |
| `float-static` | `scl::xip-static` | Direct calls, with r10 set once; the code runs from flash |
| `float-optimal` | `scl::optimal` | Direct calls, after main's GOT is patched; no r10 |
| `float-dynamic` | `scl::dynamic-newlib` | `dlopen`, `dlsym`, `dlclose` |
| `float-dynamic-xip` | `scl::xip-dynamic-newlib` | `dlopen`, `dlsym`, with r10 set for the call |
| `float-overlay` | `scl::overlay` | Direct calls, after swapping the right module into the window |

`float-overlay` is built differently. `float_add` and `float_mul` are two modules sharing one SRAM
window, so main loads the right one before each call, and it has no `calls` counter.

## Step 2: `spifs-*`, a real driver

**What it is for:** the same loaders with a library that does real work and needs things from main.

**The library** has four layers:

```text
spifs-api.cpp        the exported C API, used by name from main
fs-archive-spi.cpp   a flat filesystem: one directory, no subfolders
spi-shim.cpp         the spi_flash_* functions the filesystem calls
w25q64.cpp           the W25Q64 driver, which calls the STM32 HAL in main
```

| Function | What it does |
|---|---|
| `spifs_init()` | Starts the SPI bus and checks the chip's ID; returns 0 if a Winbond chip is found |
| `spifs_format()` | Erases 1 MB and writes an empty directory |
| `spifs_create(name, data, len)` | Writes a file |
| `spifs_read(name, buf, cap, &len)` | Reads a file |
| `spifs_list(i, name, &size)` | Gets entry `i` of the directory |
| `spifs_count()` | Counts the files |
| `spifs_delete(name)` | Marks a file deleted, without erasing it |

**What the app does:**

1. Load the library. Its HAL imports must resolve against main.
2. Check the chip, and format.
3. Create `hello.txt`, `nums.txt` and `scl.txt`.
4. List them, and print each one's contents.
5. Delete `hello.txt`, and list again.

**Output** of `spifs-reloc` on the WeAct, shortened:

```text
  spifs.rel.o loaded + linked
  W25Q64 detected, SPI up
  flatfs formatted
  created 3 files; count=3
  -- ls --
    hello.txt (13B)
    nums.txt (10B)
    scl.txt (15B)
  -- cat --
    hello.txt: "Hello WeAct!"
    nums.txt: "1 2 3 4 5"
    scl.txt: "loaded by name"
  deleted hello.txt; count=2
```

**What a correct run proves:**

- **Imports from main work:** an import left unpatched faults on the first HAL call, usually in
  `spifs_init()`.
- **Several source files link into one library,** with internal calls, a C++ class and more data.
- **The results come back from a physical chip,** so a loader bug that corrupts the library's code or
  data shows up as wrong file contents.

`spi-static-full-ram` is this example on `scl::static`, where the whole library runs from RAM. Main fills
an `elf_loader::symbol_table` with the addresses the library needs. The `spifs-*` examples need the W25Q64
fitted.

**Limits of the filesystem**, which matter only if you reuse it outside these examples:

- Nothing reopens a filesystem after a reset: only `spifs_format()` makes it usable, so files can't be
  found again after a reboot.
- Deleting the newest file makes its space look free without erasing it, so the next `spifs_create()`
  writes over used flash and can read back wrong. The examples delete an older file, so they don't hit
  this.

## Step 3: `combo-*`, two libraries in turn

**What it is for:** what happens between loads. Steps 1 and 2 each load once; a real system may need to
replace one library with another.

**What the app does**, using the libraries from steps 1 and 2:

1. Read and load the float library, compute `sum` and `prod`, and keep them in main.
2. Unload it.
3. Read and load the SPI library, into the same memory where the loader allows it.
4. Format, and write the two values to `calc.txt`.
5. Read the file back and print it.

**Output** of `combo-reloc` on the WeAct, shortened:

```text
combo-reloc: float module -> unload -> spi module (RAM/ET_REL)
  floatlib: sum(2.500, 3.500)=6.000  prod=8.750
  float module unloaded (arena reused)
  read back calc.txt: "sum=6.000 prod=8.750"
combo-reloc: done
```

**What a correct run proves:**

- The second library works in memory that held the first, so nothing stale from the first remains.
- The first library's values survive, because they live in main.
- Libraries are read one at a time, when needed, not all at the start.
- The file holds numbers computed by a library that is no longer loaded, so the two really did run in turn.

**What "unload" means differs per loader**, and this step is what shows it:

| Example | Loader | What unloading does |
|---|---|---|
| `combo-reloc` | `scl::reloc` | Nothing to undo: main keeps no state, and the arena is reused |
| `combo-static-full-ram` | `scl::static` | Reverts its patches, byte for byte |
| `combo-static` | `scl::xip-static` | The `scl::Module` destructor restores main's GOT |
| `combo-optimal` | `scl::optimal` | Deregisters the first and re-patches main for the second; both are linked at the same window |
| `combo-dynamic` | `scl::dynamic-newlib` | `dlclose` reverts a journal of its changes, restoring main's imports |
| `combo-dynamic-xip` | `scl::xip-dynamic-newlib` | `dlclose` |
| `combo-overlay` | `scl::overlay` | No unload: loading the second module overwrites the first in the window |

The same step runs on the Pi-Zero, on all 9 loaders: see step 5.

## Step 4: `api-*`, proving the new APIs are used

Here the library is deliberately trivial, because the subject is how imports get resolved. Each
library imports `app_gain()` from main, so `float_scale(2, 3)` returns 9 only if the new table
supplied the address.

| Example | Loader | How main's table is built |
|---|---|---|
| `api-static-boot-table` | `scl::static` | At compile time, with `SCL_REGISTER_SYM` |
| `api-optimal-boot-table` | `scl::optimal` | At boot, with `build_blob()` |
| `api-dynamic-xip-semihost-table` | `scl::xip-dynamic-newlib` | At boot, read over semihosting from a reduced copy of the ELF |
| `api-nolib-eager` | `scl::dynamic-nolib` | From `dl_set_symtab()`, with eager binding |

The tests get stricter here. The optimal and nolib examples also export a decoy `app_gain()` that
returns 99, so a wrong path gives a wrong answer instead of an accidental pass. `api-nolib-eager` also
checks that the lock count does not change during calls, which shows no loader code ran.

## Step 5: `pizero/` `float`, `spifs`, `combo`, the same code on two cores

Now the board changes, and the fixtures deliberately do not. The WeAct fixture files are compiled
from their own folders, so anything that breaks comes from the new environment:

- two Cortex-M33 cores instead of one Cortex-M4;
- the µOS++ III SMP kernel;
- a fixed order: load on core 0 at boot, then start SMP, then use the library.

Every app follows the same four stages, implemented once in `common/pizero.cpp`:

```text
app_load()       core 0, before the kernel, core 1 in the bootrom
app_start()      core 0, kernel running, core 1 still in the bootrom
app_on_core(n)   core 0, then core 1, with SMP running
app_together(n)  both cores at once
```

Library data must now be visible to both cores: `calls` reads 2 after core 0's calls and 4 after
core 1's. The apps also check every value themselves, and a wrong one fails the run. On the WeAct, most
examples only print values and reach a completion marker, so a wrong value there can still pass.

Only one fixture file changes. The board has no W25Q64 and no STM32 HAL, so
`fixtures/w25q64-rp2350.cpp` is the same class over 1 MB of the board's own QSPI flash. Writing stops
code running from flash, so at this step all writes happen in `app_start()`, before core 1 runs.

**How each app differs from its WeAct version:**

| App | Before SMP, on core 0 | With SMP, on core 0 then core 1 |
|---|---|---|
| `float` | Load the library | Call `float_add` and `float_mul`, check the results, and check `calls` |
| `spifs` | Load; format; create 3 files; list; read `hello.txt`; delete it | List the 2 files left, and read them |
| `combo` | Load float; compute; unload it; load spifs; format; write `calc.txt` | Read `calc.txt`, and check its contents |

`spifs` on the Pi-Zero, shortened:

```text
spifs-reloc: libraries loaded at boot on core 0
  flash JEDEC id: 85 20 18
  flash detected
  flatfs formatted
  created 3 files; count=3
  ...
  starting SMP

--- on core 0 ---
    nums.txt: 1 2 3 4 5
    scl.txt: loaded by name

--- on core 1 ---
    nums.txt: 1 2 3 4 5
    scl.txt: loaded by name

RESULT: PASS
```

Two other differences from the WeAct:

- The chip is a Puya, and `spifs_init()` accepts only Winbond's ID, so the Pi-Zero driver reports a good
  ID as Winbond's. The firmware prints the real ID.
- `combo` unloads with `lib_unload()`, in each loader's own way as in step 3, and prints both addresses.
  In one run, `spifs` landed about 1 KB to 3 KB past where `floatlib` was on the RAM and flash loaders, so
  they reuse the memory. `dynamic-newlib` uses the heap, and placed it about 23 KB lower.
- Writing and unloading all happen before core 1 starts. Nothing here unloads with SMP running.

## Step 6: `slib`, `provcons`, `cpp`, `ovfix`, libraries that use each other

The fixtures now come from the WeAct tests, and they grow in order:

| App | Fixtures | What it adds |
|---|---|---|
| `slib` | `mylib1`, `slib` | Constructors checked by value, internal calls, a function and data imported from main |
| `provcons` | `libprov`, `libcons`, `mylib2` | One library imports from another, so load order matters |
| `cpp` | `cpp3`, `c2`, `c1`, `mylib1` | C++ classes, recursion, heap, float, and libraries that call each other in a cycle |
| `ovfix` | `ov1`, `ov2`, `ovcpp` | Overlay swaps at boot; a swap back restores the initial data from flash |

The import cycle in `cpp` is the hard part. Imports can't all be resolved until every library is
loaded, so missing imports are only checked after the last load, through `libs_ready()`. Loaders
differ here too:

- `scl::reloc` links the cycle together with `load_set()`, so its calls are direct.
- `scl::optimal` binds them in `resolve_all()`.
- The r10 loaders go through a cross-call trampoline with a lookup at call time.
- `scl::static` has no cyclic imports, so `cpp` is not built for it. This is why `cpp` has 7 firmwares.

The fixtures also print a trace of each call through main, so the console shows the calls happening.

## Step 7: `drivers`, libraries that touch the chip

These are the RP2350 versions of the WeAct driver libraries. They use the chip's registers themselves
instead of asking main to.

- `libled` blinks the LED, which you can watch.
- `libuart` writes to the UART, sharing the board's console lock.
- `libspi` moves bytes through the SPI block in internal loopback and counts mismatches.

`libuart` prints only to the UART, so a semihosting console shows its character count but not its text.

## Step 8: `cppeh`, exceptions across libraries

This is the most demanding step for the loaders, because a throw walks through code that the loader
placed and patched.

| Call | Throws in | Caught in |
|---|---|---|
| `eh_selftest(1)` | `libcppeh` | `libcppeh` |
| `ehx_catch_cross(1)` | `libcppeh` | `ehx_catch` |
| `eh_throw(1)` | `libcppeh` | main |

`libcppsh` adds virtual calls, where the vtable must be patched correctly, and `eh_dyncast` adds RTTI.

The firmware needs more for this step:

- the full `libsupc++`, because the `nano.specs` one has no unwind tables;
- main's unwind table as its own output section, so that the linker sorts it;
- `call_lib` written with unwind directives, in `common/arena.S`, because a throw passes through it.

Exceptions run on one core at a time, because this toolchain's `libstdc++` is single-threaded.

## Step 9: `flashsmp`, writing flash with both cores running

Every earlier step wrote flash before core 1 started. This one lifts that limit, and it is the first
to use `app_together()`.

1. Core 0 formats the filesystem while core 1 keeps running code from flash.
2. Both cores create 12 files each, at the same time, and read each one back.
3. Between writes, each core runs a loop in flash and checks its result.
4. The run checks that all 24 files exist and that each core was parked for the other's writes.

Writing stops execute-in-place, so while one core writes, the other must run nothing from flash. A
forced TIMER1 interrupt parks it in SRAM until the write ends. Both cores must first enable
coprocessor 7, because the bootrom's flash code uses it. This is the `scl::rp2350-flash` helper in the
loader, and `include/scl/rp2350-flash.h` has its rules.

The filesystem has no lock of its own, so `flashsmp` serialises calls into it. A sector erase also
stops interrupts on both cores for tens of milliseconds, so this approach does not suit hard real-time
work.

## Where to start

- **New to SCL:** read `float-reloc`, then `spifs-reloc`.
- **Choosing a loader:** compare `pizero/loaders/*.cpp`. Each is about 30 lines, and they do the same
  job. `docs/LOADER-CHOICE.md` covers the choice itself.
- **Porting to a new board:** bring up `float` first, then `spifs`, then `cpp`, then `cppeh`.
- **Something failed:** find the lowest step that fails. What that step added is the first suspect.

## How to run them

```sh
./run-all-weact.sh                 # steps 1 to 4, on the WeAct board
./pizero/run-all-pizero.sh         # steps 5 to 9, on the Pi-Zero
./pizero/run-all-pizero.sh cppeh   # one app
```

## What the ladder leaves out

- PSRAM, interrupts, missing-import reporting and heavy calls from both cores at once are in
  `pico2-pizero-shared/` in `SCL-TESTS-shared-new`.
- Nothing here unloads a library while SMP is running.
- Nothing here throws on both cores at the same moment.
