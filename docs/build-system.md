# The example build system — folders, CMake, Make and run.sh explained

This document walks through **how an example is put together and how it builds** —
the folder layout, every build file (`CMakeLists.txt`, `fixtures/Makefile`,
`run.sh`), and the two CMake machineries an example pulls in (the `SCL-shared`
loader package and the `platform/` board support). It is the reference for reading
an existing example or wiring a new app the same way. Companion:
`import_in_project_full.md` (the per-model recipe).

---

## 1. Folder structure of an example

Every WeAct example has the **same** top-level shape (`pizero/` has its own, one build for all nine):

```
<example>/                         e.g. spifs-dynamic-xip/
├── CMakeLists.txt                 the app build (the only file you really write)
├── run.sh                         build fixture + firmware, flash + run on WeAct
├── openocd-weact.cfg              OpenOCD probe/target config (CMSIS-DAP + STM32F4x)
│
├── SCL-shared/                    ← git submodule: THE LOADER (the only dependency)
│   ├── CMakeLists.txt             defines every scl::* target (INTERFACE source libs)
│   ├── include/  src/             the loader sources (compiled INTO your firmware)
│   ├── cmake/  tools/             gen-symtab host tool, embed helper
│   └── common-elf-reader/         ← nested submodule: the ELF32 parser
│
├── platform/                      board support WE ship (a real app uses its own)
│   ├── cmake/
│   │   ├── arm-none-eabi.cmake         freestanding toolchain (reloc/static-xip/optimal)
│   │   ├── arm-none-eabi-hosted.cmake  hosted/newlib toolchain (static-full-ram, dynamic)
│   │   └── embed-symtab.cmake          (dynamic) bakes main's symbols into flash
│   ├── tools-config.sh
│   └── test/platforms/
│       ├── common/                board-agnostic bits (crt.cpp …)
│       ├── board/                 bare WeAct: startup.c, semihost.c, board.c, libc-shim.c
│       └── board-hosted/          hosted WeAct: newlib vendor tree + HAL + dl:: facade
│                                  (+ cmake/platform-library.cmake defining platform::board-hosted)
│
├── fixtures/                      the loadable module(s)
│   ├── Makefile                   emits build/<name>.so or .rel.o with the model's recipe
│   ├── src/  include/             the module sources (floatlib.cpp / w25q64.cpp / …)
│   ├── xip-split.ld               (XIP models only) the R-X | RW split linker script
│   └── build/                     output — gitignored
│
├── src/                           YOUR firmware
│   ├── main.cpp                   the app (load a module, call it)
│   ├── uart.c / uart.h            USART1 PA9 115200 output helper
│   └── *.ld                       the firmware linker script (plain.ld / sections-flatfs-hosted.ld)
│
└── build/                         CMake output — gitignored
```

Two of these are dependencies you don't edit: **`SCL-shared/`** (the loader, a
submodule) and **`platform/`** (board files). The three you own are
**`fixtures/`**, **`src/`**, and the **`CMakeLists.txt`** that ties them together.

> **Bare vs hosted platform.** The `*-reloc`, `*-static`, `*-optimal` examples use
> the **bare** `platform/test/platforms/board/` tree (freestanding startup, no
> newlib). The `*-static-full-ram` and all `*-dynamic*` examples use the
> **hosted** `board-hosted/` vendor tree (newlib, heap, full STM32 HAL, the `dl::`
> facade) because those loaders need `std::vector`/`std::string` and/or `dlopen`.

---

## 2. `SCL-shared/CMakeLists.txt` — the loader package

`add_subdirectory(SCL-shared scl)` runs this file. It defines **every loader
flavour as an INTERFACE "source-code library"** — no `.a` archive is produced; the
loader's `.cpp` are *added to whichever firmware target links the alias*, compiled
with that firmware's own flags. The aliases it exports:

```
scl::common          shared core (ELF parse, resolver, relocation, log) + common-elf-reader
scl::reloc           Model A — ET_REL loader (RAM arena)
scl::static          Model B — -fPIC .so copied whole to RAM (needs SCL_HOSTED)
scl::xip-common      shared XIP primitives (segment map, relocate, compact)
scl::xip-static      Model C — XIP + r10
scl::loader-nopic    XIP no-r10 primitives
scl::optimal         Model D — XIP no-r10 fixed windows
scl::dynamic-nolib   Model F(nolib) — freestanding dlopen
scl::dynamic-newlib  Model E — hosted POSIX dlopen (needs SCL_HOSTED)
scl::xip-dynamic-nolib / scl::xip-dynamic-newlib   the XIP adapters for the dlopen loaders
scl::overlay         Model G — linker-overlay window manager (portable C; own modules)
```

Key options it honours:

- **`SCL_HOSTED`** (`OFF` by default) — set `ON` **before** `add_subdirectory` to
  pull the hosted-only parts (`relocator.cpp`, the newlib dynamic loader). Required
  by `scl::static`, `scl::dynamic-newlib`, `scl::xip-dynamic-newlib`.
- **`SCL_LOADER`** — optional convenience: `-DSCL_LOADER=optimal` aliases
  `scl::loader` to the chosen case, so a firmware can link the generic `scl::loader`.

It also defines the **`gen-symtab-host`** target (a *host* C++ tool) used by the
dynamic examples to bake `main`'s symbol table into a flash section.

You never edit this file — it comes from the pinned submodule commit. `git
submodule update --init --recursive` populates it and its nested
`common-elf-reader`.

---

## 3. `platform/` — the board support

`platform/` is everything the loader is **not**: reset/startup, the vector table,
clock/HAL init, semihosting, the UART, linker-script fragments, and (hosted) the
newlib syscalls + the `dl::` `dlopen` facade. In these examples we ship it so each
example is self-contained and flashes as-is; **a real product deletes `platform/`
and points the `CMakeLists.txt` `include()`/source lists at its own board files.**

Two ways an example consumes it:

- **Bare models (reloc / static-xip / optimal):** the example's own `CMakeLists.txt`
  lists the board sources explicitly, e.g.
  `${PLATFORM}/test/platforms/board/src/startup.c` + `semihost.c` + `board.c` +
  `common/src/crt.cpp` + `libc-shim.c`.
- **Hosted models (static-full-ram / dynamic):** it `include()`s
  `platform/test/platforms/board-hosted/cmake/platform-library.cmake`, which builds
  an INTERFACE target **`platform::board-hosted`** (all the vendor startup/HAL/
  newlib sources with `-fno-pic -fhosted`) and — for the dynamic ones — defines the
  **`scl_embed_symtab(<target>)`** function. The example links `platform::board-hosted`.

The `platform/cmake/` toolchain files pin the ABI: **hard-float only**
(`-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16`), and set
`CMAKE_C/CXX_COMPILER`, `CMAKE_OBJCOPY`, `CMAKE_OBJDUMP`, `CMAKE_SIZE_UTIL`, and
`SCL_MCU_FLAGS`.

---

## 4. `CMakeLists.txt` — the app build

This is the file you write. Its skeleton is the same across models; the parts that
vary are the **SCL target linked** and the **link options** (linker script, `-pie`,
`-u` anchors). Anatomy, in order:

```cmake
cmake_minimum_required(VERSION 3.20)
project(<example> LANGUAGES C CXX)

# guard: must be cross-compiling (fail early with the right toolchain hint)
if(NOT CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "configure with -DCMAKE_TOOLCHAIN_FILE=platform/cmake/arm-none-eabi[-hosted].cmake")
endif()

# 1) the loader (the one dependency).  Hosted models set SCL_HOSTED first.
set(SCL_HOSTED ON CACHE BOOL "" FORCE)                 # dynamic / static-full-ram only
add_subdirectory(${CMAKE_SOURCE_DIR}/SCL-shared scl)   # -> scl::* targets

# 2) the board support
set(PLATFORM ${CMAKE_SOURCE_DIR}/platform)             # bare models
#   include(${PLATFORM}/test/platforms/board-hosted/cmake/platform-library.cmake)  # hosted models

# 3) the firmware target
add_executable(<example>
    src/main.cpp src/uart.c
    <platform board sources, or ${STATIC_UNIFY_DIR}/firmware/src/dl-impl.cpp for dynamic>)
target_include_directories(<example> PRIVATE src ${SCL_or_PLATFORM include dirs})
target_link_libraries(<example> PRIVATE
    <platform target or explicit libs>
    scl::<your-model>)                                 # THE loader model
target_compile_definitions(<example> PRIVATE
    <RELOC_LIB_DIR|DL_LIB_DIR|SCL_SO_DIR>="${CMAKE_SOURCE_DIR}/fixtures/build")

# 4) link options — model-specific (linker script, -pie, keep-alive anchors)
target_link_options(<example> PRIVATE
    -T${linker_script}
    <-pie -Wl,-E for the XIP/dynamic models>
    <LINKER:-u,<sym> anchors so borrowed imports reach main's symtab>)

# 5) POST_BUILD: (dynamic) embed the symtab, then always emit .hex + .bin
scl_embed_symtab(<example>)                            # dynamic only
add_custom_command(TARGET <example> POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:<example>> $<TARGET_FILE:<example>>.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:<example>> $<TARGET_FILE:<example>>.bin)
```

What changes per model:

| Model | link target | linker script | link flags of note |
|---|---|---|---|
| A reloc | `scl::reloc` | `src/plain.ld` | `-nostdlib -nostartfiles`; firmware compiled `-fno-pic -fno-pie` |
| B full-ram | `scl::static` | `mem-sh/libs-sh/sections-sh.ld` | non-PIC hosted; links the `.so` in |
| C static-xip | `scl::xip-static` | `src/sections-flatfs-hosted.ld` | `-pie -Wl,-Bsymbolic -Wl,-E`, `-u` HAL anchors |
| D optimal | `scl::optimal` | `src/app-windows.ld` | `-pie`; windows auto-derived by the fixture build |
| E dynamic | `scl::dynamic-newlib` | (platform's `sections-dyn.ld`) | `scl_embed_symtab` + `-u` anchors; hosted |
| F dynamic-xip | `scl::xip-dynamic-newlib` | `src/sections-flatfs-hosted.ld` | `-pie`, `scl_embed_symtab`, gen-symtab POST_BUILD, `-u` anchors |
| G overlay | `scl::overlay` | `src/sections-overlay.ld` (OVERLAY block *before* `.text`, `NOCROSSREFS`) | non-PIC; module sources compiled `-fno-pic -mlong-calls`; no separate fixture `.so` |

The **`-u` (keep-alive) anchors** matter: a symbol the module imports but that
`main` never calls itself would be dropped by `--gc-sections` and never reach
`main`'s symtab/`.dynsym`. `LINKER:-u,HAL_SPI_Init` (etc.) forces it to stay.

---

## 5. `fixtures/Makefile` — building the loadable module

The fixture is compiled with the **firmware's exact MCU/FPU flags** but a
**model-specific PIC/link recipe**. This is a plain GNU Make; `run.sh` calls
`make -C fixtures TC=<toolchain-bin>`. The recipe per model:

```make
# common to all:
MCU := -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16

# A (reloc): compile -fno-pic, then a PARTIAL link keeps it ET_REL
#   $(CXX) $(MCU) -mlong-calls -fno-pic -ffreestanding -c *.cpp
#   $(LD)  -r *.o -o mylib.rel.o

# B / E (RAM .so): plain -fPIC ET_DYN
#   $(CXX) $(MCU) -fPIC -shared -nostartfiles -nostdlib -nodefaultlibs -Wl,-E *.cpp -o mylib.so

# C / F (XIP): r10-based, SPLIT into R-X | RW by xip-split.ld
#   PIC := -fpic -msingle-pic-base -mpic-register=r10 -mno-pic-data-is-text-relative
#   $(CXX) $(MCU) $(PIC) -shared -nostartfiles -nostdlib -nodefaultlibs \
#          -Wl,-E -Wl,-Bsymbolic -Wl,--gc-sections -T xip-split.ld *.cpp -o mylib.so
#   readelf -l mylib.so   # MUST show two LOAD segments: "R E" and "RW"

# D (optimal): -fno-pic at an AUTO-DERIVED window; build.sh emits windows.generated.ld

# G (overlay): NO fixtures/Makefile and NO .so — the module sources live in
#   fixtures/src/ and are compiled INTO the firmware (-fno-pic -mlong-calls); the
#   overlay linker script places them in the shared SRAM window. "Loading" is a
#   runtime memcpy, not a build artifact.

# every dynamic/SPI fixture also:  strip --strip-debug mylib.so   (else -g3 -> ~870KB, too big for the RAM dlopen)
```

Recurring flags and *why*:

- **`-nostdlib -nodefaultlibs`** (C/C++ fixtures) — without it the default
  startfiles merge `.text` + data into one `RWE` segment; the XIP loader needs the
  clean **R-X | RW split**, so this keeps them separate.
- **`-Wl,-E`** — export the module's symbols so the loader can find them by name.
- **`-Wl,-Bsymbolic`** — bind the module's *own* intra-lib calls directly, so only
  genuine external imports go through the GOT/PLT.
- **`-Wl,--unresolved-symbols=ignore-all`** — the module's imports (HAL, libc) are
  left `UND` on purpose; the loader resolves them at load against `main`.
- **HAL includes** (SPI fixtures) — the module `#include`s `stm32f4xx_hal.h`, so the
  Makefile adds the vendor include dirs (`.../stm32f4-hal`, `.../cmsis`, and the
  `firmware/include` that has `stm32f4xx_hal_conf.h`) plus `common-elf-reader/include`.

The SPI fixture is 4 TUs — `w25q64` (driver) + `spi-shim` (the SPI handle + MspInit)
+ `fs-archive-spi` (the flat-fs) + `spifs-api` (the by-name exports `spifs_init/…`)
— compiled to objects then linked into one `spifs.so` / `spifs.rel.o`.

---

## 6. `run.sh` — build, flash, run on the WeAct

Each `run.sh` is the same three steps; copy it verbatim for a new app.

```sh
HERE=<this dir>;  PLATFORM="$HERE/platform"
TC=<arm-none-eabi bin>;  export PATH="$TC:$PATH"

# 1) fixture
make -C "$HERE/fixtures" TC="$TC"

# 2) firmware (configure once, then build)
[ -d build ] || cmake -S "$HERE" -B build \
    -DCMAKE_TOOLCHAIN_FILE="$PLATFORM/cmake/arm-none-eabi[-hosted].cmake" \
    -DSCL_PLATFORM=board -DSCL_BOARD=weact
cmake --build build --target <example> -j

# 3) flash + run (OpenOCD: program the .hex, enable semihosting, reset run)
openocd -f openocd-weact.cfg \
    -c init -c "reset halt" -c "program build/<example>.hex" \
    -c "arm semihosting enable" -c "reset run" \
    [ -c "sleep $RUN_MS" -c shutdown ]     # RUN_MS set => auto-stop, else Ctrl-C
```

- **`RUN_MS`** (ms) auto-shuts-down after that long; leave unset for an interactive
  run. SPI examples need a large window (W25Q64 erase is slow); the full-in-RAM
  `scl::static` examples need the largest (verbose per-symbol loader logging over
  semihosting).
- Output appears on **two** channels: **USART1 PA9 @ 115200 8N1** (via `src/uart.c`)
  and the **semihosting console** (OpenOCD stdout). `main` prints each line to both.
- `openocd-weact.cfg` selects the on-board CMSIS-DAP probe (`vid_pid 0x1a86 0x8011`,
  HID interface 2) and the STM32F4x target.

---

## 7. The end-to-end build graph

```
run.sh
 ├─ make -C fixtures ─────────────► fixtures/build/<name>.so|.rel.o   (module, model recipe + strip)
 │
 └─ cmake --build build
      ├─ add_subdirectory(SCL-shared) ─► scl::<model>   (loader .cpp compiled into the firmware)
      │      └─ common-elf-reader::elf-reader
      ├─ platform sources / platform::board-hosted     (startup, HAL, semihost, dl:: facade)
      ├─ src/main.cpp + src/uart.c
      │      ⇒ link with -T<script> [-pie] [-u anchors]  ► build/<example>.elf
      ├─ (dynamic) gen-symtab-host reads the .elf ─► .scl_symtab ─► objcopy --update-section  (bake main's symbols)
      └─ objcopy -O ihex/-O binary                       ► build/<example>.hex / .bin
                                                             │
                                              openocd program + semihosting ► runs on WeAct,
                                              reads the module over semihosting from fixtures/build/
```

At runtime the firmware reads the module bytes **over semihosting** from
`fixtures/build/` on the host (the `*_LIB_DIR`/`SCL_SO_DIR` compile-define is that
path), loads it with its `scl::<model>` loader, resolves the module's imports back
into `main`, and calls the exports — printing results to UART + semihosting.
