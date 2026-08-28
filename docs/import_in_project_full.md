# Building an app with SCL-shared — a step-by-step guide for all seven loader models

This is the practical companion to **`LOADER-CHOICE.md`**. That document tells you
*which* loader model fits your product (RAM vs XIP, `r10` vs no-`r10`, and which
module *format*). This one is the recipe: for each model, the **same six steps** —
build the fixture, wire the firmware CMake, the linker script, provide the blob,
resolve imports + call by name, and the model-specific gotchas — with the exact
flags, tied to a **runnable example in this repo that is verified on WeAct F411CE
hardware**. Every model below has a `float-*`, `spifs-*` and `combo-*` example you
can copy verbatim.

Target board throughout: **WeAct F411CE (BlackPill)**.

---

## The canonical example template (copy this shape)

Every example here — and every app you build on SCL-shared — has the SAME minimal
shape. Only **two** things are needed on top of your own `src/`:

```
my-app/
├── SCL-shared/          the loader — a git submodule (the ONLY dependency).
│                        Cloned recursively (it carries common-elf-reader). Provides
│                        scl::reloc / static / xip-static / optimal / dynamic-* as
│                        INTERFACE *source* libraries — .cpp that compile INTO your
│                        firmware with your flags; never a .a archive.
├── platform/            board support. In these examples we SHIP it (WeAct startup,
│                        HAL, semihosting, the dl:: facade, flat-fs) because the
│                        examples are self-contained. A REAL app already has its own
│                        startup/HAL — drop platform/ and point the include()s at
│                        your board files instead.
├── fixtures/            the loadable module(s): sources + a Makefile that emits .so
│                        / .rel.o with the exact recipe the chosen model needs.
├── src/  (main.cpp)     your firmware + its linker script.
├── CMakeLists.txt       add_subdirectory(SCL-shared scl) + include(platform/…) + app.
└── run.sh               build fixture + firmware, flash the WeAct, run (semihosting).
```

**Get the loader as a submodule and clone recursively** (it carries
`common-elf-reader` as its own nested submodule):

```sh
git submodule add <url>/SCL-shared.git SCL-shared
git submodule update --init --recursive          # pulls common-elf-reader too
```

**Consume it in CMake** — one line gives you every `scl::*` target:

```cmake
add_subdirectory(${CMAKE_SOURCE_DIR}/SCL-shared scl)   # -> scl::reloc / static / xip-static / optimal / dynamic-*
target_link_libraries(my_fw PRIVATE scl::reloc)        # pick your model's target
```

For the **hosted** models (B static-full-ram, E/F dynamic-newlib) set
`set(SCL_HOSTED ON CACHE BOOL "" FORCE)` **before** the `add_subdirectory` — those
loaders use `std::vector`/`std::string` and need the hosted parts.

**Build / flash / run** is one script (see any `run.sh`): build the fixture with
the toolchain, `cmake` + build the firmware, then `openocd` programs the WeAct and
enables semihosting. `RUN_MS=10000 ./run.sh` auto-shuts-down after 10 s; the SPI
examples need a longer window (flash erase), the full-in-RAM ones longer still
(verbose per-symbol loader logging).

## The seven models at a glance

| # | Model | Example prefix | SCL target | Module built | Runs from | `r10` | Call site |
|---|---|---|---|---|---|---|---|
| **A** | RAM / `ET_REL` | `*-reloc` | `scl::reloc` | `-mlong-calls -fno-pic -r` | RAM (arena) | no | `sym`→ptr, or bare (`-pie`) |
| **B** | RAM / `-fPIC .so` | `*-static-full-ram` | `scl::static` | `-fPIC -shared` | RAM (buffer) | no | **bare** direct |
| **C** | XIP + `r10` | `*-static` | `scl::xip-static` | `-fPIC -msingle-pic-base` split | flash (text) | **yes** | **bare** (`set_r10`) or `call_sym` |
| **D** | XIP, no `r10` | `*-optimal` | `scl::optimal` | `-fno-pic` fixed windows | flash-window (RAM) | no | **bare** direct |
| **E** | dynamic, hosted POSIX | `*-dynamic` | `scl::dynamic-newlib` | `-fPIC -shared` | RAM (buffer) | no | `dlopen`/`dlsym`→ptr |
| **F** | dynamic, XIP | `*-dynamic-xip` | `scl::xip-dynamic-newlib` | `-fpic -msingle-pic-base` split | flash (text) | **yes** | `dlopen`/`dlsym`→ptr + `r10` |
| **G** | overlay (own modules) | `*-overlay` | `scl::overlay` | `-fno-pic -mlong-calls`, link-placed | RAM window (`memcpy`) | no | **bare** direct (link-bound) |

The first six load a module whose address is unknown until runtime. **Model G is
different in kind:** the "modules" are built *with the firmware* and bound at link
time to a shared SRAM window; "loading" is a `memcpy` — no ELF, no relocation, no
`dlsym`. Use it to time-share SRAM between your *own* build-time routines, not for
third-party `.so` (see Model G below and `LOADER-CHOICE.md` §3e).

Each of A–F has three worked examples — `float-<prefix>` (minimal), `spifs-<prefix>`
(the W25Q64 SPI driver + flat-fs), `combo-<prefix>` (load a float module, keep the
result, unload, load the SPI module, persist it); **G adds the same three**
(`float-/spifs-/combo-overlay`). All 20 are verified on WeAct. A and B run from RAM
and differ only in module *format* (`ET_REL` object vs a real `-fPIC .so`); **E and
F are the POSIX `dlopen` family** — handle-based `dlsym`→pointer instead of a
direct by-name call.

> **Can't rebuild the fixture?** If you were handed a binary `.so` + `.h` only,
> your choice is forced: a stock `-fPIC .so` runs **from RAM** via **Model B**
> (`scl::static`, direct by-name calls) or **Model E** (`scl::dynamic-newlib`,
> `dlopen`) — never XIP/reloc/optimal (those need the fixture's own build flags).
> See `LOADER-CHOICE.md` §0.
>
> There is also a freestanding, no-libc `dlopen` (`scl::dynamic-nolib` /
> `scl::xip-dynamic-nolib`) — same API as E/F without newlib. The examples here use
> the newlib flavour; swap the target + drop `SCL_HOSTED` for the nolib one.

---

## 0. What is true for every model (do this once)

**Hard-float only — and no `double`.** Every toolchain is
`-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16`. Build the fixtures
with the *exact* same flags: a hard/soft mismatch links (sometimes) but faults at
the first float call. The Cortex-M4 FPU is **single-precision**, so any `double`
math — `log()`, a `double`→`float` cast — pulls a *hidden* libgcc helper
(`__aeabi_d2f`) that a loaded `.so` **cannot bind** (the link fails with
`hidden symbol __aeabi_d2f … referenced by DSO`). Keep fixtures on `float`
(`logf`, `sinf`, `sqrtf`), never `double`.

**The module imports symbols from your firmware** — its `printf`/`malloc`, the C++
runtime, the STM32 **HAL** (the `spifs-*` modules import `HAL_SPI_*`/`HAL_GPIO_*`
and `memcpy`/`memset`), and libgcc helpers. `main` must **provide and export**
those; how differs per model (Step 5) and is the subject of
`resolving-module-imports.md`. **List what a module needs** with:

```sh
readelf -W --dyn-syms module.so   | awk '$7=="UND"'   # imports (ET_DYN)
readelf -W -s        module.rel.o | awk '$7=="UND"'   # imports (ET_REL)
```

Use `-W`, or long names truncate (`HAL_SPI_Transmit[...]`) and you chase a symbol
that is actually present. **A symbol the module imports but that `main` leaves
UNDEFINED is the #1 cause of a load-time fault** — see the HAL gotcha in §7.

**You provide the blob bytes.** The loader takes bytes; where they come from — a
RAM buffer, an on-chip flat-fs, straight from semihosting — is your ~15-line
`provide()`/`xload()`/`dlopen()` (Step 4).

---

## Model A — RAM execution, `ET_REL` (`scl::reloc` — see `float-reloc/`, `spifs-reloc/`, `combo-reloc/`)

**Use when:** RAM is available and you want the leanest possible loader. The
module is a compiled-but-unlinked object relocated into a RAM arena.

### Step 1 — Build the fixture as an `ET_REL` object

```sh
arm-none-eabi-g++  $MCU  -mlong-calls -fno-pic  -Os -ffreestanding -fno-common \
    -std=c++20 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit \
    -c mylib.cpp -o mylib.o
arm-none-eabi-ld   -r  mylib.o [more.o …]  -o mylib.rel.o          # -r keeps it ET_REL
readelf -h mylib.rel.o | grep Type      # -> REL (Relocatable file)
```

`-mlong-calls -fno-pic` makes every external reference an `R_ARM_ABS32` literal
the loader patches — no GOT/PLT/r10. `-r` is a *partial* link: it stays
`ET_REL`, symbols left `UND`. Multiple TUs? compile each, then one `ld -r` over
all of them (that is how `spifs-reloc` links `w25q64`+`spi-shim`+`fs-archive-spi`+
`spifs-api` into one `spifs.rel.o`).

### Step 2 — Firmware CMake

```cmake
add_subdirectory(${SCL} scl)
add_executable(my_fw src/main.cpp ${PLATFORM_SOURCES})
target_link_libraries(my_fw PRIVATE scl::reloc)               # loader + ELF reader; that's all
target_compile_options(my_fw PRIVATE -fno-pic -fno-pie -Os)   # plain firmware (see Step 5 for -pie)
target_link_options(my_fw PRIVATE
    -T${CMAKE_SOURCE_DIR}/src/plain.ld
    -nostdlib -nostartfiles -Wl,--gc-sections)
```

### Step 3 — Linker script

A normal flash+RAM script — copy `relocatable-shared/test-relocatable/platform/reloc.ld`.
The module arena is an ordinary `.bss` array in `main`; **nothing to reserve**.

### Step 4 — Provide the blob

Read the `.rel.o` into a RAM buffer (semihosting shown), then hand it to the loader:

```cpp
std::size_t sz = read_file(RELOC_LIB_DIR "/mylib.rel.o", g_img, sizeof g_img);
```

### Step 5 — Resolve imports + call

Two sub-cases, by how many symbols the module imports:

- **Few imports** — a small resolver, the native reloc form (a resolved pointer):
  ```cpp
  loader::result<std::uint32_t> resolve(void*, std::string_view n) {
      if (n == "host_f") return addr(&host_f);
      return loader::unexpected(loader::error::SYMBOL_NOT_FOUND);
  }
  reloc::module m = *reloc::load(blob, std::span(g_arena), {resolve, nullptr});
  auto fn = reinterpret_cast<unsigned(*)(unsigned)>(reloc::sym(m, "my_lib_fn"));
  unsigned r = fn(1);                              // resolved function pointer
  ```
  `reloc::load_set(images, arena, resolve)` links several mutually-dependent
  modules together.

- **Many imports (e.g. the HAL)** — don't hand-write the table. Link the firmware
  **`-pie -Wl,-E`** with **`-u`** anchors and resolve generically against main's
  own `.dynsym` with `fw::link_context::from_self()` — **zero table**. This is
  what `spifs-reloc` does (it imports `HAL_SPI_*`, `memcpy`, …):
  ```cpp
  auto self = *fw::link_context::from_self();
  loader::symbol_source rs{ [](void* c, std::string_view n){
      return static_cast<fw::link_context*>(c)->resolve(n); }, &self };
  reloc::load(blob, std::span(g_arena), rs);
  ```
  Full recipe and the `-pie` requirement: **`resolving-module-imports.md`**.

### Step 6 — Model-A gotchas (from `float-reloc` / `spifs-reloc`)

- The board UART API (`board_uart_*`) is declared only under `-DSCL_BOARD_WEACT`,
  and QEMU has no UART — guard UART calls in `#if defined(SCL_BOARD_WEACT)` and
  fall back to `trace_puts` for QEMU.
- For the `from_self` path: `link-context.cpp` was **trimmed from
  relocatable-shared** — copy `src/link-context.cpp` from the reference tree into
  your firmware sources, or `from_self()` is undefined at link.
- The board `sections.ld` `INCLUDE`s `mem.ld` (add `-L<ldscripts dir>`) and
  `mem.ld` hard-reserves `_simage_arena_size = 64K` — supply a local `mem.ld`
  with it `= 0` (and put your `-L` first) if you use your own arena, or RAM
  overflows. Compile `-fpic` for the `-pie` variant.

**Cost:** RAM (whole code copied in). **Per-call:** none.

---

## Model B — RAM execution, real `-fPIC` `.so` (`scl::static` — see `spi-static-full-ram/`, `combo-static-full-ram/`)

**Use when:** you want a genuine `.so` (dlopen-style) rather than an `ET_REL`
object, still running fully in RAM. `r10`-free (PC-relative GOT), bare direct
calls. This is the classic static patcher — a bit more machinery than Model A.

### Step 1 — Build the fixture as a plain `-fPIC` `.so`

```sh
arm-none-eabi-g++  $MCU  -fPIC -fno-common -O0 -g3 -std=c++20 -ffreestanding \
    -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit \
    -c mylib.cpp -o mylib.o
arm-none-eabi-g++  $MCU  -fPIC  -shared -nostartfiles -nostdlib -Wl,-E \
    -Wl,--gc-sections -Wl,--unresolved-symbols=ignore-all  *.o  -o mylib.so
readelf -h mylib.so | grep Type      # -> DYN (Shared object file)
```

Plain `-fPIC` — **no** single-pic-base, **no** split script, **no** windows.

### Step 2 — Firmware CMake

The static loader is `scl::static`; the firmware is **non-PIC** with the loader's
own GOT linker scripts, and links the `.so` (for main's GOT slots):

```cmake
project(my_fw C CXX)
# the static loader uses <string_view> (hosted) and the firmware is non-PIC:
string(REPLACE "-fpic " ""         CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
string(REPLACE "-ffreestanding " "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")   # + the C flags
add_subdirectory(${SCL} scl)
add_executable(my_fw src/main.cpp ${PLATFORM_SOURCES} ${HAL_SOURCES})
target_link_libraries(my_fw PRIVATE scl::static ${FIX}/mylib.so)
target_link_options(my_fw PRIVATE
    -T${ST}/ldscripts/mem-sh.ld -T${ST}/ldscripts/libs-sh.ld -T${ST}/ldscripts/sections-sh.ld
    -nostartfiles --specs=nano.specs --specs=nosys.specs -Wl,--gc-sections)
```

Copy `static-shared/test-static/test-drv/CMakeLists.txt` — it is the authoritative
wiring (platform sources, the `test-static/test-static` system tree, the three
`*-sh.ld` GOT scripts).

### Step 3 — Linker script

You don't write one — the three `ldscripts/{mem,libs,sections}-sh.ld` from
`static-shared/test-static/test-static/` set up main's GOT/PLT so `patch_main`
can wire the bare calls.

### Step 4 — Provide the blob

`load_file()` reads the `.so` over semihosting **and** loads it in one call — no
separate `provide()` needed:

```cpp
alignas(8) static uint8_t g_exec[48*1024];        // the .so runs entirely here
```

### Step 5 — Resolve imports + call

The static loader resolves imports against a **`symbol_table` you populate** with
`add_global(name, addr)` (a third resolution style — see
`resolving-module-imports.md` §2c), then `patch_main` makes the exports bare
direct calls. `spi-static-full-ram`:

```cpp
#include "module.h"                               // scl::Module
static elf_loader::symbol_table g_syms;
g_syms.add_global("HAL_SPI_Init", (uint32_t)(uintptr_t)&HAL_SPI_Init);   // one per import
scl::Module m;
m.load_file(DL_LIB_DIR "/mylib.so", g_exec, sizeof g_exec, &g_syms);     // copy→RAM, relocate, patch, ctors
int r = spifs_init();                             // ← bare direct call by name
```

### Step 6 — Model-B gotchas (from `spi-static-full-ram`)

- **Strip `-ffreestanding`** from the toolchain flags: the loader includes
  `<string_view>`/`<string>` which error with *"not available in freestanding
  mode"*. **Strip `-fpic`** too — this firmware is non-PIC.
- Keep the fixture and firmware the **same float ABI** (both hard-float here).
- The `.so` is both a link input (GOT slots for the exports) *and* the runtime
  blob — one file, two roles.

**Cost:** RAM + a real GOT/PLT (more machinery than `ET_REL`). **Per-call:** none.

---

## Model C — XIP with `r10` allowed (`scl::xip-static` — see `float-static/`, `spifs-static/`, `combo-static/`)

**Use when:** you must execute from flash to save RAM, and can reserve `r10`. The
R-X segment runs from flash; only the RW segment (GOT/data) is copied to RAM,
`r10` = its base.

### Step 1 — Build the fixture as split, r10-PIC

```sh
PIC="-fpic -msingle-pic-base -mpic-register=r10 -mno-pic-data-is-text-relative"
arm-none-eabi-g++ $MCU $PIC -O0 -ffunction-sections -fdata-sections -std=c++20 \
    -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit \
    -shared -nostartfiles -nostdlib -nodefaultlibs -Wl,-E -Wl,-Bsymbolic \
    -Wl,--gc-sections -T xip-split.ld  *.cpp  -o mylib.so
readelf -l mylib.so | awk '$1=="LOAD"'      # must show R E  +  RW (two segments)
```

The `R E | RW` **split is mandatory** — the XIP loader relocates them separately.
`-nostdlib -nodefaultlibs` is required: without it the default startfiles merge
text+data into one `RWE` segment and the loader rejects it.

### Step 2 — Firmware CMake

`-pie`, links `scl::xip-common` plus the static-loader sources and the flat-fs,
and `-l:` the `.so`:

```cmake
add_executable(my_fw src/main.cpp ${PLATFORM_SOURCES}
    ${SCL}/src/xip/static/adapter.cpp   ${SCL}/src/xip/xip-cross-call.c    # (also bundled by scl::xip-static)
    ${SCL}/src/port/pic-trampoline.c    ${SCL}/src/image-memory.cpp
    ${SCL}/src/link-context.cpp         ${SCL}/src/dynamic-info.cpp
    ${T3}/src/fs-archive.cpp ${T3}/src/flash-prog.c)                       # the flat-fs
target_link_libraries(my_fw PRIVATE scl::xip-common ${LIBSUPCXX} ${LIBGCC})
target_link_options(my_fw PRIVATE
    -pie -nostartfiles --specs=nano.specs
    LINKER:-Bsymbolic LINKER:-E LINKER:--unresolved-symbols=ignore-in-shared-libs
    -T${CMAKE_SOURCE_DIR}/src/sections-flatfs-hosted.ld       # NO _sxip windows
    LINKER:-u,HAL_SPI_Init LINKER:-u,memcpy …                 # keep the module's imports in .dynsym
    -L${FIX} -l:mylib.so -lstdc++)
```

Copy `static-shared/test-xip/test-xip-static-direct/CMakeLists.txt`.

### Step 3 — Linker script

A plain flatfs script with **no `_sxip` windows** (PIC needs none) — copy
`test-xip-static-direct/src/sections-flatfs-hosted.ld`.

### Step 4 — Provide the blob (with a mandatory pre-bind)

Read the `.so` over semihosting into the flat-fs, but **first pre-bind its
function imports** — XIP runs the `.got.plt` in place, so those slots must
already be resolved:

```cpp
xip::stat::bind_host_imports(std::span<std::byte>{buf, sz});   // BEFORE fs_add
fw::fs_add(name, buf, sz);
```

Skip it and load fails: `[xip] unbound PLT import '<name>' - prelink missing`.

### Step 5 — Resolve imports + call

The module's imports resolve against main's `.dynsym` (kept there by the `-u`
anchors). For the call, prefer **bare direct** — `xload()` already patched main's
GOT (`patch_host_got`); set the PIC base once:

```cpp
scl::Module m;
xload("mylib.so", m);                 // reads flat-fs, loads XIP, patches main's GOT
set_r10(m);                           // r10 = module GOT base, once
int r = spifs_init();                 // ← bare direct call
```

Safe when **one** module is resident and `main` is `-fpic` without a fixed
pic-register. With several modules live, swap `r10` per call:
`call_typed<Ret>(m, "name", args…)` (the `u32`-only `call_sym1/2` can't carry
`float`/struct — `call_typed` does).

### Step 6 — Model-C gotchas (from `float-static` / `spifs-static`)

- **`-nostdlib -nodefaultlibs` on C fixtures** or the `R E | RW` split collapses.
- **`bind_host_imports` before `fs_add`** for every function import.
- Fixtures must be (re)built **after** cmake configure (configure copies the
  standard `.so`s; your XIP recipe must overwrite them).
- `scl::xip-static` bundles `adapter.cpp` + `xip-cross-call.c`; you may link it
  instead of listing those two.

**Cost:** `r10` + a small per-call indirection (none for the bare form).

---

## Model D — XIP without `r10` (`scl::optimal` — see `float-optimal/`, `spifs-optimal/`, `combo-optimal/`)

**Use when:** you must execute from flash **and** cannot touch `r10`. Code runs
from a fixed SRAM window; calls are **bare** literals with no per-call cost. The
price — fixed windows — is paid by an automatic build step, not by hand.

### Step 1 — Build the fixture `-fno-pic` at auto-derived windows

`optimal-shared/fixtures-src/build.sh` (or a copy) reads your firmware's RAM
geometry, measures the module, **packs it top-down below the stack**, relinks at
the derived base, and emits `fixtures/windows.generated.ld`:

```sh
arm-none-eabi-g++ $MCU -fno-pic -mlong-calls -O0 -nostdlib -nodefaultlibs \
    -fno-exceptions -fno-rtti … -shared -nostartfiles -Wl,-E \
    -Wl,--unresolved-symbols=ignore-all -T nopic-split.ld  *.cpp  -o mylib.so
# the script fills windows.generated.ld with _sxip_text/data_* — NO typed addresses
```

### Step 2 — Firmware CMake

`-pie`, links the `optimal` helper (over `scl::loader-nopic`), and `-l:` the `.so`:

```cmake
include(${SCL}/cmake/scl-lib.cmake)
add_library(scl-loader-nopic INTERFACE)
target_sources(scl-loader-nopic INTERFACE
    ${SCL}/src/xip/static/nopic.cpp ${SCL}/src/image-memory.cpp ${SCL}/src/link-context.cpp)
target_link_libraries(scl-loader-nopic INTERFACE scl::xip-common)
add_library(optimal INTERFACE)
target_sources(optimal INTERFACE ${SCL}/src/loader.cpp)
target_link_libraries(optimal INTERFACE scl-loader-nopic)

target_link_libraries(my_fw PRIVATE optimal ${LIBSUPCXX} ${LIBGCC})
target_link_options(my_fw PRIVATE
    -pie LINKER:-Bsymbolic LINKER:-E LINKER:--unresolved-symbols=ignore-in-shared-libs
    -T${CMAKE_SOURCE_DIR}/src/app-windows.ld
    LINKER:-u,HAL_SPI_Init …                       # if the module imports the HAL (spifs-optimal)
    -L${FIX} -l:mylib.so -lstdc++)
```

### Step 3 — Linker script

`src/app-windows.ld` holds only device geometry plus `INCLUDE
windows.generated.ld` (found via `-L${FIX}`) — **no window addresses by hand**.
Copy `optimal-shared/test/common/ldscripts/sections-flatfs-*-nopic.ld`.

### Step 4 — Provide the blob

Read the `.so` over semihosting into a RAM buffer or the flat-fs (`provide()`);
no pre-bind step — the no-pic loader resolves imports at load.

### Step 5 — Resolve imports + call — the cleanest of the four

One call loads, resolves the module's imports (against main's `.dynsym`), and
patches main's GOT; then the exports are **bare** direct calls, **no r10, no
helper**:

```cpp
optimal::Module mods[1]; std::span<const std::byte> blobs[1];
blobs[0] = provide("mylib.so");
optimal::load_and_link(blobs, 1, mods);
int r = spifs_init();                              // ← bare direct call
```

### Step 6 — Model-D gotchas (from `floatapp` / `spifs-optimal`)

- Windows are **auto-derived** — never type an address; an `ASSERT` in the script
  fails the link if `.bss` would collide with the window (shrink firmware/fixture).
- EH/RTTI libraries are reached through a typed pointer, and hidden libgcc helpers
  need anchoring — see `resolving-module-imports.md` / the `optimal` tests.

**Cost:** a build-time placement step (automatic). **Per-call:** none.

---

## Model E — dynamic `dlopen`, hosted POSIX / RAM (`scl::dynamic-newlib` — see `float-dynamic/`, `spifs-dynamic/`, `combo-dynamic/`)

**Use when:** you want the familiar Linux **`dlopen`/`dlsym`/`dlclose`/`dlerror`**
contract — load a `.so` *by path*, look symbols up *by handle*, run destructors on
close — and you have a hosted (newlib) firmware with a heap. This is **not** a
direct by-name call: you get a `void*` handle and a function pointer from `dlsym`.
The loader binds imports at link and patches the module's GOT, with a **journaled
revert** so `dlclose` restores byte-exact state.

### Step 1 — Build the fixture as a plain `-fPIC` `.so`

```sh
MCU="-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16"
arm-none-eabi-g++ $MCU -fPIC -shared -nostartfiles -Wl,-E -O0 \
    -std=c++20 -nostdlib -nodefaultlibs  mylib.cpp -o mylib.so     # one RW-mapped image, no r10
```

No r10, no split — a plain `ET_DYN` copied whole into a RAM buffer at load
(`test-dynamic/fixtures-src/build.sh`).

### Step 2 — Firmware CMake — **hosted**, `SCL_HOSTED=ON`

Configure with the **hosted** toolchain (`cmake/arm-none-eabi-hosted.cmake`) and
force `SCL_HOSTED` before the loader is added (it pulls `relocator.cpp` /
`std::vector`):

```cmake
set(SCL_HOSTED ON CACHE BOOL "" FORCE)      # BEFORE add_subdirectory(${SCL_ROOT} scl)
add_executable(my_fw src/main.cpp ${PLATFORM_SOURCES}
    ${DL_FACADE}/dl-impl.cpp)               # the POSIX dl:: facade (vendored)
target_link_libraries(my_fw PRIVATE scl::dynamic-newlib)
```

### Step 3 — Linker script

An ordinary hosted `-pie` sections script; no `_sxip` windows, no split. Copy
`dynamic-shared/test-dynamic/test1`'s.

### Step 4 — Provide the blob

`dlopen` takes a **path**; the vendored `dl-impl.cpp` reads it over semihosting
into a RAM buffer for you — you write nothing:

```cpp
void* h = dlopen("/fixtures/mylib.so", 0);        // reads + loads into RAM
```

### Step 5 — Resolve imports + call — the **embedded symbol table**

The module's `UND` imports (your `printf`/`malloc`, HAL, the C++ runtime) resolve
against a **symbol table baked into flash** from main's own ELF by the host tool
`gen-symtab`, wired with one line:

```cmake
scl_embed_symtab(my_fw)                     # bakes main's exported symbols into flash
# + keep-alive anchors so unreferenced imports still reach the table:
target_link_options(my_fw PRIVATE LINKER:-u,HAL_SPI_Init LINKER:-u,memcpy …)
```

Then the call is handle-based (a real function pointer, not a direct call):

```cpp
auto fn = (unsigned (*)(unsigned)) dlsym(h, "library_function");
unsigned r = fn(42);
```

### Step 6 — Model-E gotchas

- **Hosted toolchain + `SCL_HOSTED=ON`** — the freestanding toolchain won't build
  `scl::dynamic-newlib` (`std::vector`); set the option *before* the loader is added.
- **`scl_embed_symtab(target)` + `-u` anchors** are the resolution mechanism — a
  missing anchor ⇒ `dlsym`/load fails to bind that import (`--gc-sections` dropped it).
- `dlopen` returns `nullptr` on a missing/invalid file — check `dlerror()`.

**Cost:** a heap + the embedded symtab in flash. **Per-call:** none (plain pointer).

---

## Model F — dynamic `dlopen`, XIP + r10 (`scl::xip-dynamic-newlib` — see `float-dynamic-xip/`, `spifs-dynamic-xip/`, `combo-dynamic-xip/`)

**Use when:** you want the `dlopen` contract **without libc** — a tiny `dl::`
facade (`dl::open`/`dl::sym`/`dl::close`) over any `byte_source`, no `std::vector`,
no heap requirement. Same plain `-fPIC` `.so` as Model E for the RAM form; for
execute-in-place, build the **r10-split** `.so` (Model C's recipe) and load it with
**`scl::xip-dynamic-nolib`** — text runs from flash, `r10` = the RAM GOT base.

### The deltas from Model E

1. **Firmware CMake** links `scl::dynamic-nolib` (RAM) or `scl::xip-dynamic-nolib`
   (XIP); **no `SCL_HOSTED`** — it builds with the ordinary hard-float freestanding
   toolchain (`cmake/arm-none-eabi.cmake`).
2. **Fixture:** RAM form = Model E's plain `-fPIC -shared`; XIP form = **Model C's**
   `-fpic -msingle-pic-base -mpic-register=r10 … -T xip-split.ld` (must show `R E | RW`).
3. **Provide + XIP pre-bind:** the RAM form is `dl::open(byte_source)`; the XIP form
   runs the `.got.plt` in place, so **pre-bind its function imports before loading**
   (as in Model C).
4. **Resolve imports:** the freestanding `dl` binds against a resolver / embedded
   symtab exactly like the XIP loaders; no POSIX facade needed.
5. **Call:** `auto p = dl::sym<Fn>(h, "name"); p(args…);` — handle-based pointer.

The XIP freestanding port uses VFP asm (`vpush {d0-d7}`), so it is **hard-float
only** — which is now the sole ABI here anyway.

**Cost:** none beyond the loader; XIP form costs `r10`. **Per-call:** none (RAM) /
r10 base (XIP).

---

## Model G — overlay (`scl::overlay` — see `float-overlay/`, `spifs-overlay/`, `combo-overlay/`)

**Use when:** you have a fixed set of your *own* routines that don't all need to be
resident at once, and want to time-share SRAM for the cost of a `memcpy`. This is
**not** runtime loading — the modules are built *with* the firmware. There is no
`.so`/`.rel.o`, no ELF parse, no relocation, no resolver: the modules are bound at
**link time** to one shared SRAM window and called directly by name. "Loading" a
module copies its bytes from flash into the window. Only one is resident at a time.

### Step 1 — Write the module sources (plain, `-fno-pic -mlong-calls`)

The modules are ordinary `.c`/`.cpp` in `fixtures/src/`, compiled with the firmware
but with `-fno-pic -mlong-calls` (they run in the SRAM window and call back into
flash across more than a Thumb-`BL`'s reach). No section attributes needed — the
linker script gathers each module's objects into its overlay section (Step 3).

```c
// fixtures/src/ovadd.c  (float-overlay module 1)
float ov_add(float a, float b) { return a + b; }
```

### Step 2 — Firmware CMake

Compile the module sources into the firmware with the overlay flags, link
`scl::overlay`, and use the overlay linker script:

```cmake
add_subdirectory(${CMAKE_SOURCE_DIR}/SCL-shared scl)          # -> scl::overlay
add_executable(my_fw src/main.cpp src/uart.c ${PLATFORM_SOURCES}
    fixtures/src/ovadd.c fixtures/src/ovmul.c)                # the overlay modules
set_source_files_properties(fixtures/src/ovadd.c fixtures/src/ovmul.c
    PROPERTIES COMPILE_OPTIONS "-fno-pic;-mlong-calls")
target_link_libraries(my_fw PRIVATE scl::overlay)
target_link_options(my_fw PRIVATE -T${CMAKE_SOURCE_DIR}/src/sections-overlay.ld ...)
```

A module that borrows the STM32 HAL (the `spifs-overlay` driver) needs that HAL
compiled into `main` (like the dynamic examples) — the module's calls to it are
bound at link time.

### Step 3 — The overlay linker script (the one real piece of work)

Reserve an SRAM window as its own `MEMORY` region, then an `OVERLAY` block places
every module at the **same VMA** (the window) with its own flash **LMA**:

```ld
MEMORY { ... OVL (rwx) : ORIGIN = 0x2001C000, LENGTH = 16K }   /* the window */

  OVERLAY : NOCROSSREFS {
    .ovadd { KEEP(<ovadd.obj>(.text .text.* .rodata* .data*)) }
    .ovmul { KEEP(<ovmul.obj>(.text .text.* .rodata* .data*)) }
  } > OVL AT> FLASH
  _ovadd_lma = LOADADDR(.ovadd);  _ovadd_size = SIZEOF(.ovadd);
  _ovmul_lma = LOADADDR(.ovmul);  _ovmul_size = SIZEOF(.ovmul);
  _ovl_vma   = ADDR(.ovadd);      /* the shared run address */
```

Two rules that cost real debugging time:

- **Place the `OVERLAY` block *before* the general `.text`** in the script. An
  input section lands in the *first* matching output section, so the overlay's
  explicit per-object selectors must come first — otherwise the general
  `*(.text .text.*)` claims the module code and the overlay ends up empty
  (`SIZEOF(.ovN) == 0`). (`EXCLUDE_FILE` on `.text` is *not* a reliable substitute.)
- **`NOCROSSREFS`** makes `ld` fail the link if one module ever references another —
  they can never be co-resident, so such a reference would be a latent HardFault.

For a multi-file module (the spifs driver) gather all four objects into one section
by object path; the `combo-overlay`/`spifs-overlay` scripts are the working template.

### Step 4 — Build the descriptor table (from the linker symbols)

No blob to provide; `main` reads the linker-emitted LMA/size/VMA:

```cpp
extern std::uint8_t _ovadd_lma[], _ovadd_size[], _ovmul_lma[], _ovmul_size[], _ovl_vma[];
scl_overlay_module mods[2] = {
    { _ovl_vma, _ovadd_lma, (uint32_t)(uintptr_t)_ovadd_size, "ovadd" },
    { _ovl_vma, _ovmul_lma, (uint32_t)(uintptr_t)_ovmul_size, "ovmul" },
};
scl_overlay_region ovl; scl_overlay_init(&ovl, mods, 2);
```

### Step 5 — Load + call (direct by name)

```cpp
scl_overlay_load(&ovl, 0);   // memcpy ovadd -> window + DSB/ISB
float s = ov_add(2.5f, 3.5f);
scl_overlay_load(&ovl, 1);   // memcpy ovmul -> window (overwrites ovadd)
float p = ov_mul(2.5f, 3.5f);
```

The exports were bound to the window address at link time, so once the right module
is resident the call is an ordinary direct call. **Imports are resolved at link
time too** — there is no runtime resolver, `.dynsym`, or embedded symtab for
overlay. Values kept in `main` survive a window swap because they live in `main`,
not the module (this is exactly what `combo-overlay` shows).

### Step 6 — Model-G gotchas

- **OVERLAY before `.text`**, and `NOCROSSREFS` (Step 3).
- Each module must **fit the window** — an `ASSERT(SIZEOF(.ovN) <= _ovl_window_size, …)`
  in the script catches overflow at link time.
- The window is carved off SRAM (below the stack); size it for the *largest* module.
- Only one module is valid at a time — a call into a non-resident module's export
  runs whatever bytes currently fill the window (garbage). Load, then call.

**Cost:** the modules are part of your build (not independent artifacts); one shares
the window at a time. **Per "load":** a `memcpy`. **Per-call:** none.

---

## 4½. Reusing memory — the unload / reload cycle

To run a **second** library after the first (`use lib1 → unload → load lib2 →
use lib2 → …`) you reuse the same region each model loads into: the RAM arena
(A), the RAM buffer (B), the reserved SRAM windows (C, D).

The one thing to get right is **main's GOT**. Models B, C and D patch main's own
GOT so `name()` is a direct call — so before switching libraries you must either
**unload** (which restores/re-points main's GOT) or let the next load re-point
it. Model A never patches main (its calls are resolved pointers), so you simply
overwrite the arena.

> **Rule:** after a library is unloaded its exports are gone — only ever call the
> library that is *currently* loaded. Two libraries that must be resident *at the
> same time* are not a swap: give each its own arena slice / buffer, or (C, D)
> load them **together as a set** rather than one-then-the-other.

### A — `scl::reloc`: no main state to restore, reuse the arena

```cpp
auto m1 = *reloc::load(img1, std::span(g_arena), rs);
… reloc::sym(m1, "fn1") …
// "unload" = discard m1; the ET_REL loader does NOT run .fini_array. Reuse the arena:
read_mod("lib2.rel.o");                       // overwrite g_img
auto m2 = *reloc::load(img2, std::span(g_arena), rs);
… reloc::sym(m2, "fn2") …
```

### B — `scl::static`: byte-exact unload (dtors + restore main + unpublish)

```cpp
{ scl::Module m1; m1.load_file("lib1.so", g_exec, sizeof g_exec, &g_syms);
  lib1_fn(); }                                // dtor: runs lib1's dtors, restores every
                                              //       word it wrote into main, unpublishes exports
{ scl::Module m2; m2.load_file("lib2.so", g_exec, sizeof g_exec, &g_syms);   // reuse g_exec
  lib2_fn(); }
```

`m.unload()` is the explicit form (RAII scope is the same thing). This is the
cleanest unload of the four — it is truly byte-exact.

### C — `scl::xip-static`: scoped unload (deregister + restore main's GOT)

```cpp
{ scl::Module m1; xload("lib1.so", m1); set_r10(m1); lib1_fn(); }   // dtor -> xip::stat::unload
{ scl::Module m2; xload("lib2.so", m2); set_r10(m2); lib2_fn(); }   // reuses the RW window
```

### D — `optimal`: unload deregisters; the next load re-points main's GOT

```cpp
{ optimal::Module m1[1]; auto b1 = provide("lib1.so");
  optimal::load_and_link(&b1, 1, m1); lib1_fn(); }                  // dtor -> xip::nopic::unload
{ optimal::Module m2[1]; auto b2 = provide("lib2.so");
  optimal::load_and_link(&b2, 1, m2); lib2_fn(); }                  // same window, patch_main re-points
```

**What "unload" actually does, per model:** A discards (no destructors run); **B**
runs the library's destructors and restores main byte-exact; **C** and **D**
deregister the module and the fixed windows are simply overwritten by the next
load (D's `patch_main` re-points main's GOT to the new exports; C's `set_r10`
re-bases). **E/F** (`dlclose` / `dl::close`) run the module's destructors and, on
`scl::dynamic-newlib`, replay the **patch journal** to restore main byte-exact —
the closest to a desktop `dlclose`. **G** (overlay) has no unload at all: the next
`scl_overlay_load()` simply `memcpy`s a different module over the window. Only B
and the ET_DYN/`dlopen` loaders publish/unpublish into a shared symbol table.

### Worked cross-module cycle: compute in one library, persist with another

A concrete pattern that ties it together (the `combo-*` examples do exactly this
on all seven models): load a **float** library, compute `sum`/`prod`, **keep those
values in `main`**, unload it, load a **SPI-flatfs** library, and write the kept
values to a file on the on-board flash, then read them back.

- **Values survive the swap because they live in `main`.** `sum`/`prod` are plain
  `float`s on main's stack — independent of either module's memory, which is
  reused across the unload.
- **Semihost each module *when you reach it*, not both upfront.** Read
  `floatlib` over semihosting, use it, unload; *then* read `spifs` over
  semihosting into the same buffer. The load order drives the semihosting reads.
- **Two flat-fs, don't confuse them** (models C/D). The on-chip **XIP flat-fs**
  (`fw::fs_*`) holds the `.so` *modules* you load; the **SPI flat-fs** (inside the
  spifs module, on the W25Q) holds the *data file* you write. `fs_format()`
  (module store) and `spifs_format()` (data store) are different filesystems.
- **Model D — link both `.so` at the same window.** The no-pic model links each
  library at a fixed SRAM window, so to *swap* two of them into one window, build
  both at the **same** `_sxip_text/data` base (size the window to the larger; the
  smaller fits). The `combo-optimal` fixture build derives the window from the SPI
  module and links `floatlib.so` at that same base. Models A/B/C reuse a plain
  arena/buffer and need no such co-location.

The `combo-*` examples carry this cross-module cycle on each loader model (the
same sequence, only the load/unload calls differ per model as in §4½ above) —
all seven verified on WeAct F411CE hardware. **`combo-overlay` (G)** is the purest
form of the pattern: the float and SPI modules literally **time-share one SRAM
window**, so the "unload" *is* loading the next module (a `memcpy` overwrites the
first), and `sum`/`prod` survive precisely because they were kept in `main`.

---

## 5. Resolving a module's imports — the styles

Whichever *runtime-loading* model, `main` must satisfy the library's `UND` symbols.
There are four ways; pick by loader (and a fifth non-way — overlay resolves at link
time, so it has no runtime resolver at all):

| Style | How | Used by |
|---|---|---|
| Hand resolver table | `if (n=="x") return addr(&x);` | Model A (few imports) |
| `.dynsym` / `from_self` | `-pie -Wl,-E` + `-u` anchors, `link_context::from_self()` | Models A(`-pie`), C, D |
| `symbol_table` | `g_syms.add_global(name, addr)` | Model B (`scl::static`) |
| **embedded symtab** | `scl_embed_symtab(target)` + `-u` anchors (host `gen-symtab` bakes main's symbols into flash) | Models **E, F** (dynamic) |
| **link-time (none at runtime)** | the module is linked with `main`; the linker binds every import to a fixed address | Model **G** (overlay) |

Full treatment, the `-pie` requirement, and the gotchas: **`resolving-module-imports.md`**.

> **Why a runtime symbol table at all** — if headers already declare every export,
> why `.dynsym`/an embedded symtab? Because a header is compile-time *types* with no
> address; a runtime load needs a name→address map on the device. See
> `resolving-module-imports.md` §3½ (incl. "can we force `.symtab` into `PT_LOAD`?").

---

## 6. Side-by-side recap

| | **A — RAM/ET_REL** | **B — RAM/`.so`** | **C — XIP+r10** | **D — XIP no-r10** | **E — dlopen/RAM** | **F — dlopen/XIP** | **G — overlay** |
|---|---|---|---|---|---|---|---|
| Example | `*-reloc` | `spi-/combo-static-full-ram` | `*-static` | `*-optimal` | `*-dynamic` | `*-dynamic-xip` | `*-overlay` |
| Target | `scl::reloc` | `scl::static` | `scl::xip-static` | `optimal` | `scl::dynamic-newlib` | `scl::xip-dynamic-newlib` | `scl::overlay` |
| Module build | `-mlong-calls -fno-pic -r` | `-fPIC -shared` | `-fPIC -msingle-pic-base` split | `-fno-pic`, auto-windows | `-fPIC -shared` | `-fpic -msingle-pic-base` split | `-fno-pic -mlong-calls`, link-placed |
| Module addresses | none | none | none | derived by build | none | none | link-time (the window) |
| Separate artifact? | yes (`.rel.o`) | yes (`.so`) | yes (`.so`) | yes (`.so`) | yes (`.so`) | yes (`.so`) | **no** — built into the firmware |
| Firmware link | plain (or `-pie`) | non-PIC + GOT scripts | `-pie` | `-pie` | hosted `-pie`, `SCL_HOSTED=ON` | hosted `-pie`, `SCL_HOSTED=ON` | non-PIC + overlay ldscript |
| Import resolution | table / `from_self` | `symbol_table` | `.dynsym` + `bind_host_imports` | `.dynsym` (`load_and_link`) | embedded symtab | embedded symtab | **link time (none at runtime)** |
| Call in source | `sym`→ptr / bare | **bare** | **bare** (`set_r10`) / `call_typed` | **bare** | `dlsym`→ptr | `dlsym`→ptr + r10 | **bare** (link-bound) |
| "Load" cost | copy+relocate | copy+patch | copy RW seg | copy RW seg | copy+patch | copy RW seg | **`memcpy`** |
| Per-call cost | none | none | r10 swap (none if bare) | none | none | r10 swap | none |
| Runs from | RAM | RAM | flash | flash-window | RAM | flash | RAM window |

---

## 7. Gotchas that bite in more than one model

These are the ones that actually cost time building the 17 examples in this repo —
each is a real fault seen on the WeAct, with its fix.

- **Match float ABI, and use `float` not `double`.** Fixture↔firmware ABI must be
  identical; and a `double` in a fixture (`log()`, `(double)x`) needs the *hidden*
  libgcc `__aeabi_d2f`, which a `.so` cannot bind → the firmware link fails
  (`hidden symbol __aeabi_d2f … referenced by DSO`). Use `logf`. (§0)

- **Compile the HAL the module needs INTO `main`.** The board platform may ship
  only part of the STM32 HAL. The `spifs-*` modules import `HAL_SPI_*`, so `main`
  must compile **`stm32f4xx_hal_spi.c` *and* `stm32f4xx_hal_dma.c`** (the SPI HAL
  pulls DMA). If any imported HAL symbol stays **UND in `main`**, it never reaches
  the resolver/symtab and the loader **`abort()`s at load** with no clear message.
  Diagnose by checking the symbol is *defined* (not `UND`) in the firmware ELF:
  `readelf -sW my_fw | awk '$8=="HAL_SPI_Init"'`. (Fixed in every `spifs-*`/`combo-*`.)

- **Strip `-g3` debug info from the fixture (RAM `dlopen`).** The hosted `dlopen`
  (Model E) slurps the **whole `.so` file** into a `std::vector` on the heap. A
  `-g3` build of the SPI module is **~870 KB** — far past the 128 KB RAM — so the
  allocation fails and `operator new` (no exceptions) calls **`abort()`** right
  inside `dlopen`. `arm-none-eabi-strip --strip-debug` drops it to ~13 KB. (In
  every dynamic fixture Makefile.)

- **Anchor the borrowed runtime.** Modules import `printf`, `operator new`, the
  EH/RTTI symbols, libgcc `__aeabi_*`, HAL, `memcpy`/`memset`. Reference them so
  they survive `--gc-sections` and land in (B) the `symbol_table`, (E/F) the
  embedded symtab, or (A/C/D) `.dynsym` — via `-u` link anchors or a `used` array.
  A missing anchor = an unresolved import at load. (§5, `resolving-module-imports.md`)

- **XIP r10 + non-u32 calls.** For the XIP models (C, F) `r10` must point at the
  module's GOT base before a call. The `u32` trampoline (`pic_call_u32`) can't
  carry `float`/struct returns — set r10 explicitly
  (`r10_val = get_got_base(h)`) then call through a **typed pointer**. See
  `call_f` in `float-static`/`float-dynamic-xip`. (§C.5, §F)

- **Two flat-fs in the SPI XIP models — don't confuse them.** The on-chip **XIP
  flat-fs** (`fw::fs_*`) holds the `.so` *modules*; the **SPI flat-fs** (inside the
  module, on the W25Q64) holds the *data file*. `fw::fs_format()` (module store) ≠
  `spifs_format()` (data store).

- **`readelf -W`** always, or truncated names mislead you.

- **CMake:** never two statements on one line (`set(a); set(b)`) — the `;` is a
  parse error. Pass an **absolute** `-DCMAKE_TOOLCHAIN_FILE=` (a relative path
  resolves against the build dir, not `-S`).

---

## 8. Worked, hardware-verified examples (copy these)

Every model has three runnable examples in this repo, each built by the six steps
above and **verified on WeAct F411CE**. When a flag is unclear, copy the example.

| Model | SCL target | Examples (copy the whole folder) |
|---|---|---|
| **A** RAM/ET_REL | `scl::reloc` | `float-reloc`, `spifs-reloc`, `combo-reloc` |
| **B** RAM full `.so` | `scl::static` | `spi-static-full-ram`, `combo-static-full-ram` |
| **C** XIP + r10 | `scl::xip-static` | `float-static`, `spifs-static`, `combo-static` |
| **D** XIP no-r10 | `scl::optimal` | `float-optimal`, `spifs-optimal`, `combo-optimal` |
| **E** dynamic dlopen (RAM) | `scl::dynamic-newlib` | `float-dynamic`, `spifs-dynamic`, `combo-dynamic` |
| **F** dynamic dlopen (XIP) | `scl::xip-dynamic-newlib` | `float-dynamic-xip`, `spifs-dynamic-xip`, `combo-dynamic-xip` |
| **G** overlay | `scl::overlay` | `float-overlay`, `spifs-overlay`, `combo-overlay` |

The `float-*` apps are the minimal shape (load, call two exports, done). The
`spifs-*` apps load the **W25Q64 SPI driver + flat-fs** module and drive the
on-board flash by name (init/format/create/list/read/delete). The `combo-*` apps
show the full cycle: load a float module, keep `sum`/`prod` in `main`, unload it,
load the SPI module, write those values to a file and read them back — proving
values survive the module swap because they live in `main`, not the module. The
`*-overlay` apps show the same shapes with the overlay technique: the modules are
built into the firmware and time-share one SRAM window (`combo-overlay` swaps a
float module and the SPI driver through the same window).

**To start a new app:** copy the example that matches your model, replace
`fixtures/` and `src/main.cpp` with yours, keep the `SCL-shared` submodule and the
`CMakeLists.txt`/`run.sh` wiring, and (for a real product) swap `platform/` for
your own board files.

---

## 9. Synthesis — the whole decision on one page

Everything above condensed. `$MCU = -mcpu=cortex-m4 -mthumb -mfloat-abi=hard
-mfpu=fpv4-sp-d16`; `$CXX20 = -std=c++20 -fno-exceptions -fno-rtti
-fno-threadsafe-statics -fno-use-cxa-atexit` (drop `-fno-exceptions -fno-rtti`
for a library that throws).

### 9.1 Which method — when to apply

| Method | Runs code from | `r10` | Module format | Choose it when… |
|---|---|---|---|---|
| **A** `scl::reloc` | RAM (arena) | free | `ET_REL` `.rel.o` | RAM is fine and you want the **leanest** loader (read+relocate+call); imports are few, or you accept `-pie`+`from_self` for many |
| **B** `scl::static` | RAM (buffer) | free | `-fPIC` `.so` (ET_DYN) | RAM is fine but you want a **real `.so`** / dlopen-style (publish/unpublish, byte-exact unload) rather than an object |
| **C** `scl::xip-static` | **flash (XIP)** | **used** | `-fPIC` split `.so` | you must **save RAM** (code stays in flash) **and can spare `r10`** |
| **D** `optimal` | flash-window | free | `-fno-pic` fixed-addr `.so` | you must run from flash **and cannot touch `r10`** (windows are auto-derived, calls are bare) |
| **E** `scl::dynamic-newlib` | RAM (buffer) | free | `-fPIC` `.so` (ET_DYN) | you want the **POSIX `dlopen`/`dlsym`/`dlclose`** contract on a hosted (newlib) firmware; journaled byte-exact `dlclose` |
| **F** `scl::dynamic-nolib` | RAM or **flash (XIP)** | XIP: used | `-fPIC` `.so` / r10-split | you want `dlopen` **without libc** (tiny `dl::` facade, no heap); XIP form runs text from flash via `scl::xip-dynamic-nolib` |
| **G** `scl::overlay` | RAM window | free | link-placed module (no `.so`) | the modules are **yours and built with the firmware**, and you want to time-share SRAM for the cost of a `memcpy` — not runtime/3rd-party loading |

### 9.2 Fixture build options (the loadable library)

| Method | Compile each TU | Link → output |
|---|---|---|
| **A** | `$MCU -mlong-calls -fno-pic -Os -ffreestanding -fno-common $CXX20` | `ld -r *.o -o lib.rel.o` |
| **B** | `$MCU -fPIC -fno-common -O0 -ffreestanding $CXX20` | `$MCU -fPIC -shared -nostartfiles -nostdlib -Wl,-E -Wl,--gc-sections -Wl,--unresolved-symbols=ignore-all *.o -o lib.so` |
| **C** | `$MCU -fpic -msingle-pic-base -mpic-register=r10 -mno-pic-data-is-text-relative -O0 -ffunction-sections -fdata-sections $CXX20` | `-shared -nostartfiles -nostdlib -nodefaultlibs -Wl,-E -Wl,-Bsymbolic -Wl,--gc-sections -T xip-split.ld` (must show **R E + RW**) |
| **D** | `$MCU -fno-pic -mlong-calls -O0 -nostdlib -nodefaultlibs $CXX20` | `-shared -nostartfiles -Wl,-E -Wl,--unresolved-symbols=ignore-all -T nopic-split.ld` at the **auto-derived** window (`build.sh` emits `windows.generated.ld`) |
| **E** | `$MCU -fPIC -O0 $CXX20` | `$MCU -fPIC -shared -nostartfiles -nostdlib -nodefaultlibs -Wl,-E *.o -o lib.so` (plain ET_DYN, RAM) |
| **F** | RAM: as **E**; XIP: as **C** (`-fpic -msingle-pic-base -mpic-register=r10 … -T xip-split.ld`) | RAM: as **E**; XIP: as **C** (must show **R E + RW**) |
| **G** | `$MCU -fno-pic -mlong-calls -O0 $CXX20` — **compiled into the firmware**, no separate link | none — the overlay linker script places each module's objects at the shared window VMA / own flash LMA |

C, D and F(XIP) **must** keep `-nostdlib -nodefaultlibs` (or the C fixtures merge to one
`RWE` segment / pull startup); B and D leave imports `UND`
(`--unresolved-symbols=ignore-all`), resolved at load.

### 9.3 Firmware (`main`) build options

| Method | SCL target(s) | Firmware compile | Link |
|---|---|---|---|
| **A** | `scl::reloc` | `-fno-pic -fno-pie -Os` (or `-fpic` for the `-pie`/`from_self` variant) | `-nostdlib -nostartfiles -Wl,--gc-sections -T reloc.ld` (add `-pie LINKER:-E LINKER:-u,<sym>` for `from_self`) |
| **B** | `scl::static` | **strip** `-fpic` **and** `-ffreestanding` (non-PIC, hosted) | `-nostartfiles --specs=nano.specs --specs=nosys.specs -T mem-sh.ld -T libs-sh.ld -T sections-sh.ld` + link `lib.so` |
| **C** | `scl::xip-common` (+ `adapter.cpp`, `xip-cross-call.c`, `pic-trampoline.c`, `image-memory.cpp`, `link-context.cpp`, `dynamic-info.cpp`, flat-fs) | `-fpic` (platform `-fno-pic -fhosted`) | `-pie -nostartfiles --specs=nano.specs LINKER:-Bsymbolic LINKER:-E LINKER:--unresolved-symbols=ignore-in-shared-libs -T sections-flatfs-hosted.ld LINKER:-u,<imports> -L$FIX -l:lib.so` |
| **D** | `optimal` (→ `scl::loader-nopic` → `scl::xip-common`) | `-fpic` (platform `-fno-pic -fhosted`) | `-pie -nostartfiles --specs=nano.specs LINKER:-Bsymbolic LINKER:-E LINKER:--unresolved-symbols=ignore-in-shared-libs -T app-windows.ld LINKER:-u,<imports> -L$FIX -l:lib.so` |
| **E** | `scl::dynamic-newlib` (**`-DSCL_HOSTED=ON`**, hosted toolchain) | hosted, `-pie` | `-pie -Wl,-E LINKER:-u,<imports>` + `scl_embed_symtab(target)`; heap/newlib via `--specs=nano.specs` |
| **F** | `scl::dynamic-nolib` (RAM) / `scl::xip-dynamic-nolib` (XIP), freestanding toolchain | `-fpic`, `-pie` | as **E** minus `SCL_HOSTED`; XIP form adds the flat-fs + pre-bind like **C** |
| **G** | `scl::overlay` (+ the module sources, compiled `-fno-pic -mlong-calls`) | `-fno-pic -fno-pie` (a HAL-using module needs the HAL compiled into main) | `-nostdlib -nostartfiles -Wl,--gc-sections -T sections-overlay.ld` (OVERLAY block *before* `.text`, `NOCROSSREFS`) |

### 9.4 Import resolution, calls, and unload — one line each

| Method | `main` provides imports via | Call site | Provisioning quirk | Unload |
|---|---|---|---|---|
| **A** | resolver table, or `-pie`+`from_self` (`.dynsym`) | `reloc::sym`→ptr (or bare, `-pie`) | copy `link-context.cpp` for `from_self` | discard, reuse arena |
| **B** | `symbol_table::add_global(name,addr)` | **bare** direct | strip `-fpic`/`-ffreestanding` | `Module::unload()` — byte-exact |
| **C** | `.dynsym` + `-u` anchors | **bare** (`set_r10`) or `call_typed` | **`bind_host_imports()` before `fs_add`** | scoped dtor (`xip::stat::unload`) |
| **D** | `.dynsym` + `-u` anchors (`load_and_link` resolves) | **bare** direct, no r10 | swap ⇒ link both `.so` at the same window | scoped dtor (`xip::nopic::unload`) |
| **E** | embedded symtab (`scl_embed_symtab`) + `-u` anchors | `dlsym`→ptr | `dlopen` reads the file for you | `dlclose` — dtors + journaled byte-exact revert |
| **F** | embedded symtab + `-u` anchors | `dlsym`→ptr + `r10`=`get_got_base(h)` | provision into flat-fs; pre-bind PLT (`patch_main_imports`) like **C** | `dlclose` — dtors + revert |
| **G** | **link time** — the module is linked with `main`, no runtime resolver | **bare** direct (link-bound to the window) | `scl_overlay_load(&r, i)` = `memcpy` LMA→window + DSB/ISB | none — the next `scl_overlay_load` `memcpy`s a different module over the window |

> If in doubt, copy the matching example folder in this repo: `float-*` (minimal),
> `spifs-*` / `spi-static-full-ram` (real W25Q64 driver), `combo-*` (load/unload/
> reload). All 17 are verified on WeAct F411CE — see §8.
