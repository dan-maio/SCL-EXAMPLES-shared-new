# Resolving a loaded module's imports — the `.dynsym` / `from_self` way

When you load a library at runtime, the library has **undefined symbols** it
imports from the application: your firmware's own ABI, plus whatever libc / libm
/ HAL / C++ runtime it borrows (it is not linked against those itself). The
loader must map each such name to an address inside your firmware. This document
explains the two ways to do that, why the clean one needs a `.dynsym`, and
exactly how to set it up — the lessons from building the `spifs` SPI-flatfs test
whose module imports the STM32 HAL from main.

---

## 1. The situation

A loaded module (an ET_REL `.rel.o` or an ET_DYN `.so`) carries a list of
undefined (`UND`) symbols. At load time the loader calls a **resolver** you
supply — `loader::symbol_source{ fn, ctx }` — once per name, and you return the
absolute address of that symbol in the running firmware.

```
readelf -sW module.rel.o | awk '$7=="UND"'      # the names the module imports
```

For the SPI-flatfs fixture, that list is `HAL_GPIO_Init`, `HAL_GPIO_WritePin`,
`HAL_SPI_Init`, `HAL_SPI_TransmitReceive`, `HAL_SPI_Receive`, `memcpy`, `memset`.

---

## 2. Two ways to satisfy it

### 2a. The hand-written table (simple, but doesn't scale)

```cpp
loader::result<std::uint32_t> resolve(void*, std::string_view n) {
    if (n == "HAL_SPI_Init") return addr(&HAL_SPI_Init);
    if (n == "memcpy")       return addr(&memcpy);
    /* … one line per imported symbol … */
    return loader::unexpected(loader::error::SYMBOL_NOT_FOUND);
}
```

This is the pattern the reference `test-relocatable` mains use. It is fine for a
handful of symbols, but it is a maintenance liability: every symbol is named
twice (string vs. address), and adding an import means editing code. For a module
that borrows a big surface (the HAL, the C++ runtime) it becomes unwieldy.

### 2b. Resolve against main's own `.dynsym` — no table (preferred)

The SCL ships `fw::link_context::from_self()`: it reads the firmware's **own
dynamic symbol table** and resolves any exported name to its address. The
resolver becomes generic — **zero per-symbol code**, and a new import needs no
change at all:

```cpp
#include "scl/link-context.hpp"

loader::result<std::uint32_t> self_resolve(void* ctx, std::string_view n) {
    return static_cast<const fw::link_context*>(ctx)->resolve(n);
}

// in main:
auto self = fw::link_context::from_self();          // parse main's _DYNAMIC once
static fw::link_context ctx = *self;
loader::symbol_source rs{ self_resolve, &ctx };
reloc::load(image, arena, rs);                       // every UND resolved via main's .dynsym
```

This is exactly how the XIP loaders already resolve library imports; `from_self`
is the same mechanism, exposed for any firmware to reuse.

### 2c. A `symbol_table` you populate (the `scl::static` loader)

The classic static loader (`scl::static`, the `-fPIC` full-in-RAM patcher) takes
a different route: you hand it an `elf_loader::symbol_table` and fill it with the
symbols the module may import:

```cpp
static elf_loader::symbol_table g_syms;
g_syms.add_global("HAL_SPI_Init", (uint32_t)(uintptr_t)&HAL_SPI_Init);   // one per import
scl::Module m;
m.load_file("lib.so", ram_buf, sizeof ram_buf, &g_syms);                // resolves against g_syms
```

Like the hand table it names each symbol, but it is the loader's native API (no
`.dynsym` needed, no `-pie`), and the table is data you can also fill from a
generated list. Use it when you're on `scl::static`; use `from_self` on the
`-pie` loaders.

---

## 3. What `from_self` requires (the catch)

`from_self()` reads `_DYNAMIC` → it only works if your firmware **has** a dynamic
symbol table. Two facts make this non-trivial:

1. **A plain static executable has no runtime symbol table.** The normal
   `.symtab` is not part of any `PT_LOAD` segment — it is stripped from the
   `.bin`/`.hex` and never present in flash at runtime. So you cannot scan
   `.symtab` on the device.
2. **`.dynsym` only exists for a *dynamic* executable.** `-Wl,-E`
   (`--export-dynamic`) does **not** create a `.dynsym` for a fully static
   (non-`-pie`) link — there is no dynamic section to attach it to. You get a
   `.dynsym` only when the image is linked **`-pie`** (or shared).

So to use `from_self`, the firmware must be linked **`-pie`** with a linker
script that emits `.dynsym`/`.dynstr`/`.hash`/`.dynamic`, and its startup must
make `_DYNAMIC` readable at runtime.

### The four ingredients

| # | Ingredient | Why |
|---|---|---|
| 1 | Link **`-pie`** + a `sections.ld` that has `.dynsym/.dynstr/.hash/.dynamic/.rel.dyn` | so an actual `.dynsym` + `_DYNAMIC` exist |
| 2 | **`-Wl,-E`** (`--export-dynamic`) | export main's globals *into* that `.dynsym` |
| 3 | **`-u <name>`** anchors (or a `used` array) for symbols main never calls itself | keep them in the link so they reach `.dynsym`; `--gc-sections` would drop an otherwise-unreferenced `HAL_SPI_Init` |
| 4 | A startup that copies `.data/.dynamic/.got` to RAM (the board `startup.c` does) | `from_self()` reads `_DYNAMIC` at its RAM address |

The `-u` anchors are the one place symbols are still *named* — but that is a
**build concern** (a linker "keep this" list), not a resolver table in code. The
resolver stays fully generic.

---

## 4. Per-loader-model summary

| Model | Firmware already `-pie`? | Import resolution |
|---|---|---|
| **static** (XIP + r10) | yes | `from_self` / the loader's own `.dynsym` walk — no table |
| **optimal** (XIP, no r10) | yes | same — no table |
| **reloc** (RAM / ET_REL) | **no** (plain by default) | a table *or* opt into `-pie` to get `from_self` |
| **static-full-ram** (`scl::static`) | no (non-PIC, hosted) | `symbol_table::add_global(name, addr)` — the loader's native API, no `.dynsym` |
| **dynamic-newlib / -nolib** (`dlopen`) | yes | **embedded symtab** — `scl_embed_symtab(target)` bakes main's symbols into flash; `-u` anchors keep them |

The **dynamic** family adds a fourth style: instead of walking a runtime `.dynsym`,
the host tool `gen-symtab` reads *main's own ELF* at build time and emits a symbol
table into a flash section, which the `dl` loader consults at `dlopen`. It still
needs the same `-u` anchors so unreferenced borrowed symbols (HAL, libgcc helpers)
survive `--gc-sections` and reach the baked table.

For the two XIP models the `.dynsym` is already there (they are `-pie`), so
`from_self`/`link-anchor` is the natural, table-free choice. The RAM/reloc model
is plain non-`-pie` by default — that is why the reference reloc mains use a
table. If your reloc firmware borrows a large surface (like the HAL), link it
`-pie` and use `from_self` instead — the loader still loads the ET_REL module
into a RAM arena exactly as before; `-pie` only changes how *main* resolves the
module's imports.

---

## 5. Gotchas hit while wiring `spifs-reloc` this way

Making a RAM/reloc firmware `-pie` for `from_self`, expect these:

- **`link-context.cpp` may not be in your loader project.** It was trimmed from
  `relocatable-shared` (whose reloc tests use tables). Copy
  `src/link-context.cpp` from the reference tree into your firmware sources, or it
  links with `undefined reference to fw::link_context::from_self()`.
- **`sections.ld` `INCLUDE`s `mem.ld`.** Add `-L<ldscripts dir>` so the include
  resolves (`cannot open linker script file mem.ld`).
- **`mem.ld` may hard-reserve a loaded-image arena** (`_simage_arena_size = 64K`).
  If you use your own `.bss` arena instead, supply a local `mem.ld` with
  `_simage_arena_size = 0` and put your `-L` **first**, or RAM overflows.
- **Compile `-fpic`** (not `-fno-pic`) for the `-pie` firmware, so its own code is
  position-independent.
- **`readelf` truncates long names** without `-W`. Verify the `.dynsym` with
  `readelf -W --dyn-syms` or you will think `HAL_SPI_TransmitReceive` is missing
  when it is only displayed as `HAL_SPI_Transmit[...]`.

Verify the firmware exports everything the module imports:

```sh
readelf -W --dyn-syms firmware | awk '$8!=""{print $8}' | sort -u   # must include every UND of the module
```

---

## 6. One-paragraph rule of thumb

If a loaded module imports more than a couple of symbols from your firmware,
don't hand-write a resolver table — link the firmware so it has a `.dynsym`
(`-pie -Wl,-E`, keep the borrowed symbols with `-u` anchors) and resolve with
`fw::link_context::from_self()`. The XIP loaders (`static`, `optimal`) already do
this because they are `-pie`; the RAM/reloc firmware can opt in the same way, at
the cost of a `-pie` link, and gets a generic, table-free resolver in return.

---

## Worked example

`examples/spifs-reloc/` is exactly this: a W25Q64 SPI-flash driver + flat-fs
loaded as an ET_REL module that imports the STM32 HAL (`HAL_SPI_*`, `HAL_GPIO_*`)
and `memcpy`/`memset` from `main`. Its `src/main.cpp` resolver is the four lines
above (`from_self`), its `CMakeLists.txt` has the `-pie` / `-u` anchors, and it
carries `src/scl-link-context.cpp` copied from the reference. `examples/spifs-static`
and `examples/spifs-optimal` show the same fixture on the two `-pie` XIP loaders,
where the `.dynsym` resolution comes for free.
