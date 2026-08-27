# Choosing how to load and call a shared library on Cortex-M

This document is a plain-English write-up of a long design discussion. The
goal was narrow and practical:

> **Write the *simplest* application that loads a shared library at runtime and
> calls its functions directly by name — with no hand-made files and no
> hand-typed addresses.**

It turns out the "best" answer is not a single project: it depends on **two
questions you have to answer first**. Once you answer them, the choice is
forced, and each choice comes with costs you cannot avoid. This document lays
out those questions, the trade-offs behind each, and which **test tree** in this
monorepo is the right starting point for each combination.

> The loader lives once in the **`SCL-shared` submodule** (five cases:
> `scl::reloc`, `scl::static`, `scl::xip-static`, `scl::optimal`, and the dynamic
> `scl::dynamic-nolib` / `scl::dynamic-newlib`); the `*-shared` folders are the
> test trees that consume it. Everything here is **hard-float only**
> (`-mfloat-abi=hard -mfpu=fpv4-sp-d16`).
>
> §§2–7 answer the original "**direct by-name call**" goal (Models RAM / XIP+r10 /
> XIP no-r10). If you instead want the classic **`dlopen`/`dlsym`** handle-based
> contract, that is the **dynamic** family — see §3d.

---

## 1. The two questions that decide everything

Everything below hinges on two yes/no questions about *your* product:

1. **Where must the library's code execute — from RAM, or in place from flash (XIP)?**
   - **RAM**: the loader copies the code into RAM and runs it there. Simple, but
     it spends RAM equal to the code size.
   - **XIP** (eXecute-In-Place): the code stays in flash and the CPU fetches
     instructions straight from flash. Only the library's writable data
     (`.data`/`.bss`/GOT) goes to RAM. Saves a lot of RAM, but the code has to
     be built so it *can* run from a fixed flash address.

2. **Is the `r10` register allowed to be reserved?**
   - True position-independent code on Cortex-M uses `r10` as a fixed "base
     register" that points at the library's data. If you can spare `r10`
     globally, the toolchain does the hard part for you.
   - If `r10` must stay a normal general-purpose register (some RTOSes, some
     existing code, some ABIs care), you cannot use that PIC model and must
     work around it.

That is the whole decision. Two questions, and the loader model falls out of
the answer.

---

## 2. The fundamental tension (why there is no single "best")

Three things everyone wants at once are **not simultaneously satisfiable**:

> **{ execute from flash (XIP) · direct calls by name · no `r10` }**
> — you can have any two, never all three.

The reason is mechanical:

- **Executing from flash** means the code must be correct at a *fixed* address
  known before it runs (flash is not relocated at load).
- **Not using `r10`** means the code cannot compute its data addresses at
  runtime relative to a base register — so those addresses, too, must be baked
  in at fixed locations.
- Fixed addresses that are baked in ⇒ **something has to decide those
  addresses**. If you refuse `r10`, that "something" is a reserved memory
  window and a build step that places the library there.

So:

- Allow `r10` → the code is genuinely position-independent → **no fixed
  addresses at all** → the simplest XIP.
- Forbid `r10` and still want XIP → you must reserve fixed windows and place the
  library in them. (The trick, below, is to make that placement *automatic* so
  it is still "no hand-made files".)
- Don't need XIP (RAM is fine) → the whole problem disappears; you just relocate
  into RAM.

---

## 3. The three models, one section each

### 3a. RAM execution — the cheapest thing that can possibly work

**Project: `relocatable-shared` (`scl::reloc`).**

Build the library as a **relocatable object** (`ET_REL`) — literally a
compiled-but-not-final-linked `.o`, the same kind of file a Linux kernel module
is:

```
-mlong-calls -fno-pic -r        # -r = partial link, keeps it ET_REL
```

The loader then does the absolute minimum:

1. read the ELF,
2. copy each section into a plain RAM arena,
3. apply the `R_ARM_ABS32` relocations (fill in the real runtime addresses),
4. run the constructors.

That's it. **No GOT, no PLT, no `r10`, no PIC, no fixed addresses, no packing
script, no reserved windows, no hand-made files.** You cannot do *less* than
"read + relocate + call" — this is the floor.

- **Cost:** RAM. The code is copied into RAM and runs there; it cannot stay in
  flash.
- **Per-call cost:** none. Once relocated, a call is a plain absolute call.

> **A second RAM form: `-fPIC` full-in-RAM via `scl::static`.** Instead of an
> `ET_REL` object you can load a plain **`-fPIC` `.so`** (`ET_DYN`) *whole* into a
> RAM buffer and run it there — the classic "static patcher". It is `r10`-free
> (PC-relative GOT) and gives **bare direct calls** (`patch_main` fills main's
> GOT). One `scl::Module::load_file()` copies it in, relocates, patches both GOTs
> and runs ctors; imports resolve against a `symbol_table` you populate with
> `add_global(name, addr)` (a third resolution style, next to a resolver table
> and `.dynsym`/`from_self`). It costs a bit more loader machinery than `ET_REL`
> (a real GOT/PLT, the app's own GOT linker scripts) but is the most "dlopen-like"
> and needs no `-pie`. Verified by the `spi-static-full-ram` example.

### 3b. XIP without `r10` — fixed windows, but generated for you

**Project: `optimal-shared` (the `optimal` target, built on `scl::loader-nopic`; namespace `xip::nopic`).**

The library is built `-fno-pic -mlong-calls` and **linked at a fixed address**.
The R-X (code) segment executes from flash; only the RW segment is copied to a
small RAM window. Because there is no `r10`, every address is absolute and baked
in — which means the library *must* be linked at exactly the address it will be
loaded to.

The obvious naive way to do that is to **hand-type** the window addresses in a
linker script (`_sxip_text_start = 0x20014000; …`). **That is exactly the
"hand-made file" we refuse.** `optimal-shared` avoids it: a build step reads the
chip's RAM geometry, measures the compiled modules, packs them top-down below
the stack, and *emits* the window addresses into a generated linker fragment.
Nothing is typed by hand; overflow fails the build.

- **Cost:** you still need fixed windows — but they are **derived by the build,
  not written by a human**. This is the price of keeping `r10` free while
  running from flash.
- **Per-call cost:** none (absolute calls).

### 3c. XIP with `r10` — true PIC, the simplest XIP of all

**Project: `static-shared` (`scl::xip-static`, the `test-xip-static-direct` app).**

The library is built as genuinely position-independent code:

```
-fpic -msingle-pic-base -mpic-register=r10 -mno-pic-data-is-text-relative -shared
```

Now the R-X segment can execute **from flash at any address, unchanged** — it is
truly relocatable. The loader:

1. copies only the RW segment (GOT + `.data`/`.bss`) to RAM,
2. sets `r10` to point at that RAM GOT,
3. applies `R_ARM_RELATIVE` and patches the GOT.

**No fixed link addresses. No reserved windows. No packing. No derived
addresses. No hand-made files.** The linker/loader handle placement entirely,
because the code doesn't care where it lives.

- **Cost:** `r10`. Every module runs with `r10` reserved as the PIC base, and
  cross-calls thunk through it — a small, permanent per-call/register cost.
- **Per-call cost:** the `r10`/GOT indirection. The by-name call needs `r10` set
  to the module's GOT base. You can do that **once** after load (`set_r10(m)`,
  with the loader's `patch_host_got` pointing main's GOT at the module) and then
  call the exports as **bare direct calls** — `spifs_init()`, verified on WeAct —
  as long as one module is resident and main doesn't use `r10` itself. With
  several modules live, swap `r10` per call via `call_sym`/`call_typed` instead
  (see `import_in_project_full.md` §B.3).

### 3d. Dynamic `dlopen` — the handle-based, POSIX-style alternative

**Project: `dynamic-shared` (`scl::dynamic-newlib` / `scl::dynamic-nolib`).**

The three models above are about making a **direct by-name** call
(`s_add(10, 5)`) work. If you don't need that — if the familiar **`dlopen` /
`dlsym` / `dlclose` / `dlerror`** contract is what you want — use the dynamic
family instead. You load a `.so` *by path*, get a `void*` **handle**, look up
each symbol *by handle* (getting a function pointer), and `dlclose` when done,
which runs the module's destructors and (on `dynamic-newlib`) replays a **patch
journal** to restore `main` byte-exact:

```c
void* h = dlopen("/fixtures/mylib.so", 0);
auto fn = (unsigned (*)(unsigned)) dlsym(h, "library_function");
fn(42);
dlclose(h);
```

Two flavours:

- **`scl::dynamic-newlib`** — hosted (newlib) firmware with a heap; the real
  **POSIX** `dlopen`/`dlsym`/`dlclose`/`dlerror` façade. Build the firmware with
  the hosted toolchain and `-DSCL_HOSTED=ON`.
- **`scl::dynamic-nolib`** — **freestanding**, no libc: a tiny `dl::open`/`dl::sym`/
  `dl::close` façade over any `byte_source`. It also has an **XIP** form
  (`scl::xip-dynamic-nolib`): the r10-split `.so` runs from flash, exactly like §3c.

Imports resolve against a **symbol table baked into flash** from `main`'s own ELF
(the `gen-symtab` host tool, one `scl_embed_symtab(target)` line) — see
`resolving-module-imports.md`.

- **Cost:** the `dlopen` machinery (a heap for `dynamic-newlib`; the embedded
  symtab in flash). It is **not** a direct by-name call — you go through a handle.
- **Use it when:** you specifically want desktop-style `dlopen` semantics
  (handles, `dlerror`, clean `dlclose`), not the tightest direct call.

---

## 4. What "direct calls" actually requires

"Direct call" means the application source literally reads:

```c
unsigned int r = s_add(10, 5);      // not sym("s_add")(10, 5), not a handle
```

For a library that is loaded at runtime, the compiler cannot know `s_add`'s
address at compile time. So there is always **one small piece of machinery** to
make the literal call work — and in every project here it is the same idea:

- The application is linked so it *has* a slot (a GOT entry) for each name it
  calls, and it is linked against the library's exported-symbol list (its `.so`)
  so the linker knows those names exist.
- After the library loads, the loader **patches the application's own GOT** so
  each name points at the freshly-loaded function.

That one step — "patch main's GOT" — is what turns `s_add(10, 5)` into a real
direct call. It is done for you by `optimal::load_and_link(...)` (no-pic) or by
the static loader's `patch_main` (r10). The alternative — a symbol lookup
returning a function pointer — is simpler to *implement* but is **not** a direct
call, so it is not what we want.

> **Rule of thumb:** direct by-name calls always cost you a GOT-patch step at
> load. That step is cheap and generic; it is *not* a hand-made file. Trying to
> avoid even that step is what leads to over-engineering.

---

## 5. What "no hand-made files / no hand-made addresses" means

This was the hard constraint throughout. It means:

- **No hand-typed addresses** anywhere (no `0x20014000` in a linker script that
  a human maintains).
- **No per-library hand-written linker scripts or glue.**
- **No manual code generation step** a person has to run and edit.

How each model satisfies it:

| Model | Fixed addresses? | Who decides them | Hand-made? |
|---|---|---|---|
| RAM (`relocatable-shared`) | none | nobody — runtime arena | **no** |
| XIP + `r10` (`static-shared`) | none | nobody — true PIC | **no** |
| XIP, no `r10` (`optimal-shared`) | yes (windows) | the build script, from RAM geometry | **no** (generated) |

All three are "no hand-made files". The difference is *where the placement work
lives*: nowhere (RAM / r10-PIC), or in an automatic build step (no-pic XIP).

---

## 6. The decision table

Answer the two questions from §1 and read off the row:

| Need XIP? | `r10` allowed? | Call style | Use | Model | Fixed addresses | Per-call cost |
|---|---|---|---|---|---|---|
| No (RAM is fine) | — | direct by-name | `relocatable-shared` | `ET_REL`, relocate into RAM | none | none |
| No (RAM is fine) | — | direct by-name | `static-shared` (`scl::static`) | `-fPIC` `.so` copied whole to RAM | none | none |
| Yes | **Yes** | direct by-name | `static-shared` (`scl::xip-static`) | `-fPIC` + `r10` base | none | `r10`/GOT indirection |
| Yes | **No** | direct by-name | `optimal-shared` | `-fno-pic` fixed windows (auto-derived) | yes, but generated | none |
| No (RAM) | — | **`dlopen` handle** | `dynamic-shared` (`scl::dynamic-newlib`) | POSIX `dlopen` on a hosted firmware | none | none (via ptr) |
| No or **Yes** | XIP: **Yes** | **`dlopen` handle** | `dynamic-shared` (`scl::dynamic-nolib` / `xip-`) | freestanding `dl::`; XIP form runs from flash | none | none / `r10` (XIP) |

The two direct-call RAM rows differ in module *format*: `ET_REL` (`scl::reloc`) is
the leanest loader; the `-fPIC` `.so` (`scl::static`) is the classic patcher —
more machinery, but a real `.so` with bare direct calls and no `-pie`. The two
**dynamic** rows trade the direct call for a `dlopen` **handle** (`dlsym` returns
a function pointer): pick them only when you want that POSIX contract explicitly.

Plain-English recommendations:

- **"I just want the simplest thing and I have RAM to spare."**
  → `relocatable-shared`. Read ELF, apply relocations, call. Nothing is cheaper.

- **"I must run from flash to save RAM, and I can give up `r10`."**
  → `static-shared`. True PIC means *zero* address machinery — the simplest XIP
  of all. You pay for it on every call via `r10`.

- **"I must run from flash *and* I cannot touch `r10`."**
  → `optimal-shared`. You need fixed windows, but the build derives them from
  the chip's memory map so nothing is hand-made, and calls stay direct with no
  per-call penalty.

There is **no fourth option** that is simpler — the tension in §2 is real, not a
gap waiting to be filled. Anything that claims all three of {XIP, direct calls,
no `r10`} with *zero* build-time placement work is either secretly using `r10`,
secretly copying to RAM, or secretly hand-placing the library.

---

## 7. Costs, stated bluntly

Every model trades one resource for another. Pick the cost you can afford:

- **`relocatable-shared`** spends **RAM** (whole code copied in) to buy total
  simplicity and zero per-call cost.
- **`static-shared`** spends **`r10` + a small per-call indirection** to buy XIP
  with zero address machinery.
- **`optimal-shared`** spends **a build-time placement step** (automatic) to buy
  XIP with `r10` free and zero per-call cost.

The one thing none of them can avoid is the **GOT-patch at load** that makes
direct by-name calls possible — and that is a feature, not a cost worth
fighting.

---

## 8. One-paragraph summary

If RAM is fine, load a relocatable object and relocate it into RAM
(`relocatable-shared`) — that is the floor and nothing beats it for simplicity.
If you need execute-in-place from flash, the question is `r10`: allow it and use
true PIC (`static-shared`) for the least machinery of all; forbid it and use the
no-pic fixed-window model (`optimal-shared`), which keeps calls direct and
`r10` free by having the build *generate* the window addresses instead of a
human typing them. In every case, direct `name(args)` calls are made possible by
one generic step — patching the application's GOT at load — and that step, not
any hand-made file, is all it takes.
