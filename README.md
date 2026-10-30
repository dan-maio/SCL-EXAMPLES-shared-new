# SCL-EXAMPLES-shared-new

WeAct F411CE examples for **SCL-shared-new**. Each example pins the loader as its
own `SCL-shared` submodule, from `/home/dan/Downloads/GIT/SCL-shared-new.git`. This
repo starts from the history of `SCL-EXAMPLES-shared` and adds:

- the pin to SCL-shared-new, in every example;
- four `api-*` examples that use the new APIs;
- `run-all-weact.sh`, which builds, flashes and checks every example.

`spifs-reloc` and `combo-reloc` now link `scl::self-resolve` instead of carrying
their own copy of `link-context.cpp`, which no longer links against the fork.

## New-API examples

| Dir | Loader | New APIs |
|---|---|---|
| `api-static-boot-table` | `scl::static` | `SCL_REGISTER_SYM`, `registered()`, `add_entries()`, lock and cache hooks |
| `api-optimal-boot-table` | `scl::optimal` | `build_blob()`, `view`, `source()`, `set_main_symbols()`, lock and cache hooks |
| `api-dynamic-xip-semihost-table` | `scl::xip-dynamic-newlib` | `collect_buffered()`, `set_symtab(const scl_sym_entry*, ...)` |
| `api-nolib-eager` | `scl::dynamic-nolib` | `dl_set_symtab()`, `dl_set_lazy_binding(false)`, lock and cache hooks |

In each one the module imports `app_gain()` from main, so `float_scale(2, 3)` only
returns 9 if the new table supplied it. The optimal and nolib examples also export
a decoy `app_gain()` returning 99, reachable only through `.dynsym`.

`api-dynamic-xip-semihost-table` reads an ELF copy reduced to the imported symbols,
made with `objcopy` after linking, because the full ELF's 90 KB `.strtab` does not
fit in RAM. That is a post-link step, though it does not change the firmware image.

## Run

```sh
git clone /home/dan/Downloads/GIT/SCL-EXAMPLES-shared-new.git
cd SCL-EXAMPLES-shared-new && git submodule update --init

./run-all-weact.sh            # every example, one by one
./run-all-weact.sh '^api-'    # a subset (regex on the name)
```

An example passes when its completion marker appears and no failure pattern does.
Logs go to `logs/weact-<timestamp>/`. The `spifs-*` and `combo-*` examples need the
W25Q64. Some tracked symlinks and tool paths are absolute, under `/home/dan`, so
the tree only builds as-is on this machine.

## WeAct F411CE status

All 25 examples passed on the WeAct F411CE with the W25Q64 fitted (2026-10-30), in
one `./run-all-weact.sh` run from clean builds, against SCL-shared-new `9f1320f`.

The 9 Pi-Zero examples in `pizero/` passed in one `./run-all-pizero.sh` run (2026-10-30),
against `9f1320f`.
The NUCLEO board and RTOS or multi-core use have not been tested; the lock and
cache hooks were checked by counting calls on a single core with no data cache.

For the same loaders on a dual-core board, see [`pizero/`](pizero/README.md) for one small app
per loader, `pico2-pizero-shared/` in `SCL-TESTS-shared-new` for the full tests, and "Dual-core: the Pi-Zero RP2350B results" in `docs/LOADER-CHOICE.md`.

## Original examples

WeAct F411CE apps that load a library at runtime and print to UART + semihosting.
Each was built by following `../docs/import_in_project_full.md` verbatim for its
situation, so they double as a live verification of that guide. Two families:

- **`float-*`** — a tiny `float_add`/`float_mul` library (no imports); the
  minimal shape of each model.
- **`spifs-*`** — the **real** thing: the reference **W25Q64 SPI-flash driver +
  a flat-fs on top**, as a loadable library. `main` calls it by name to
  init/format the on-board SPI flash, create/list/read/delete files. The module
  imports the STM32 **HAL** from `main`, so these also exercise
  `../docs/resolving-module-imports.md` (the `.dynsym` / anchor path).

| Dir | Situation | Loader | Call style |
|---|---|---|---|
| `float-reloc`  / `spifs-reloc`   | A — RAM         | `scl::reloc` (ET_REL)       | `sym`→ptr / `from_self`-resolved |
| `float-static` / `spifs-static`  | B — XIP + r10   | `scl::xip-static` (PIC)     | **bare** by-name (`set_r10` once) |
| `float-optimal`/ `spifs-optimal` | C — XIP, no r10 | `optimal` (no-pic windows)  | **bare** by-name (no r10) |

The `spifs-*` apps additionally: build the fixture from the reference
`w25q64.cpp` driver + `fs-archive-spi.cpp`; are **WeAct-only** (they drive the
physical SPI flash); resolve the module's HAL imports against `main`'s `.dynsym`
(reloc opts into `-pie` + `from_self`, the XIP models get it for free).

### `combo-*` — the cross-module load/unload cycle

Four apps (`combo-reloc`, `combo-static`, `combo-optimal`, `combo-static-full-ram`)
that combine the two fixtures on each model to demonstrate the unload/reload
cycle end-to-end: **semihost + load the float library, compute `sum`/`prod` and
keep them in `main`, unload it, then semihost + load the SPI library and write
those two values to `calc.txt` on the on-board flat-fs, read it back, print.**

They show, per model: values survive the swap because they live in `main`;
modules are semihosted **one at a time** (not upfront); the memory (arena/buffer/
window) is reused; and unload differs per model (reloc discards, static is
byte-exact, XIP+r10/no-r10 deregister and re-patch). `combo-optimal` also links
both `.so` at the **same** SRAM window. See `../docs/import_in_project_full.md`
§4½. All four verified on WeAct F411CE hardware.

### `float-static-full-ram` / `spi-static-full-ram` — the classic patcher

The Model-B variants using **`scl::static`**: the fixture built as a plain
**`-fPIC` `.so`** is copied **whole into a RAM buffer** and run there — the
dlopen-style patcher. `float-static-full-ram` is the minimal shape (the tiny
`float_add`/`float_mul` lib, no imports); `spi-static-full-ram` is the real
W25Q64 driver. Both are `r10`-free, **bare direct calls** (`patch_main`), no
`-pie`; the module's imports resolve against an `elf_loader::symbol_table` that
`main` fills with `add_global(name, addr)`. One `scl::Module::load_file()` does
copy + relocate + patch + constructors. See `../docs/LOADER-CHOICE.md` §3a and
`import_in_project_full.md` §A-bis.

## Build & run (WeAct board, SWD/CMSIS-DAP)

The SCL loader is **not vendored** — each `run.sh` clones it from git on first
run (override the location with `GIT_REMOTE=…`), then builds the fixture and the
firmware and flashes with OpenOCD + semihosting:

```sh
cd float-reloc && ./run.sh        # (or float-static / float-optimal)
```

Expected `float-*` output (semihosting console and USART1 PA9, 115200 8N1):

```
float_add(2.500, 3.500) = 6.000
float_mul(2.500, 3.500) = 8.750
floatlib calls = 2
```

The `spifs-*` apps print an `init → format → create → ls → cat → delete → ls`
walkthrough of the on-board SPI flash. All six were verified on WeAct F411CE
hardware. (A `spifs-reloc` `run.sh` also copies the STM32 HAL from the reference
repo — override its location with `REF_REPO=…` — since `relocatable-shared`
ships none.)
