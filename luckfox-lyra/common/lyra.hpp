/**
 * @file lyra.hpp
 * @brief What every Luckfox Lyra example shares, so that an app shows only what it does.
 *
 * The board is an RK3506: 3x Cortex-A7, 128 MiB of DRAM, and NO execute-in-place
 * flash that applications may use. Everything runs from DRAM. Storage is the
 * microSD card, reached through FatFs (the board's boot card is never formatted).
 *
 * Boot order, on every example:
 *   app_load()      core 0, before the kernel, other cores still parked: load the libraries
 *   app_start()     core 0, kernel running: anything that needs the card or the kernel
 *   app_on_core(n)  on core 0, then 1, then 2, with SMP running
 *   app_together(n) on all cores at once (weak, most apps do not need it)
 */
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

#include "scl/symtab-register.h"

// ---- the app --------------------------------------------------------------------------
extern const char* const kAppName;
bool app_load();
bool app_start();
void app_on_core(unsigned core);
void app_together(unsigned core);   // weak: all cores at once, after app_on_core

// ---- one loaded library: loaders/<loader>.cpp implements these two ----------------------
struct Lib {
    unsigned slot = 0;       ///< Which of the app's libraries: 0 to 3.
    std::uint32_t got = 0;   ///< r10 for the r10 models, else 0.
};
bool lib_load(Lib& lib, unsigned slot, const char* base);   ///< base is the fixture's name.
void* lib_sym(const Lib& lib, const char* name);
void lib_unload(Lib& lib);                                  ///< Its memory can be loaded into again.
const char* lib_first_missing();                            ///< After every library is loaded.

/// Libraries that import from each other, into slots 0 to n-1. scl::reloc must link them together.
bool lib_load_set(Lib* libs, const char* const* bases, unsigned n);
/// Bind what is left, and report any import still missing, by name. Call after the last load.
bool libs_ready();

/// One function of a library, and the r10 it needs.
struct Fn { void* f = nullptr; std::uint32_t got = 0; };
Fn lib_fn(const Lib& lib, const char* name);
extern "C" std::uint32_t call_lib(void* fn, std::uint32_t got, std::uintptr_t a, std::uintptr_t b, std::uintptr_t c, std::uintptr_t d);
/// Inline: when an exception crosses it, the caller's own unwind table covers it.
inline std::uint32_t call(const Fn& f, std::uintptr_t a = 0, std::uintptr_t b = 0) { return call_lib(f.f, f.got, a, b, 0, 0); }
void show(const char* what, std::uint32_t v);  ///< Prints "  what = v".
void show(const char* what, std::uint32_t v, std::uint32_t want);  ///< Also checks it.

// Enter a library with r10 = its GOT base, whichever loader it came from.
int lib_call(const Lib& lib, void* fn, std::uintptr_t a = 0, std::uintptr_t b = 0, std::uintptr_t c = 0, std::uintptr_t d = 0);
float lib_callf(const Lib& lib, void* fn, float a, float b);

// ---- helpers ------------------------------------------------------------------------
void say(const char* s);                       ///< To the UART and to semihosting.
void sayf(const char* fmt, ...);               ///< printf-style, unsigned arguments in the examples.
void expect(bool ok, const char* what);        ///< A failure makes the example fail.

/// The board's user LED (GPIO1_A0): `times` blinks, 80 ms on and off. The boot path
/// uses it to show which core is running and to signal the verdict: steady on for
/// PASS, blinking for FAIL. An app may call it too.
void blink_led(unsigned times);
std::span<const std::byte> read_host_file(const char* name);

/// "flash": the Lyra has none for apps, so an XIP image is provisioned into a static
/// DRAM image region and runs in place from there. The slot is 64 KiB.
std::span<const std::byte> write_to_flash(std::span<std::byte> image, std::uint32_t flash_addr);
std::uint32_t flash_address(unsigned slot);    ///< Where slot's image will live in DRAM.
std::uint8_t* example_arena(unsigned slot);    ///< RAM to load slot into.
unsigned flash_parks(unsigned core);           ///< Always 0: this board never parks a core.

void app_lock();                               ///< A spinlock across all cores, recursive.
void app_unlock();
inline constexpr std::size_t kArenaBytes = 24u * 1024u;  // each slot's share of arena.S

// ---- the SD card, the board's storage -------------------------------------------------
/// Init the card and mount its FAT volume, once. Strong only in firmwares that link
/// common/board-sd.cpp; the weak default reports that this app has no card.
bool board_sd_begin();
/// The one file the SD fixtures read and write, under tests/ on the boot card.
extern "C" int board_sd_store(const void* data, std::uint32_t len);
extern "C" int board_sd_load(void* data, std::uint32_t len);

/// main's exports: what the libraries may import.
extern const scl_sym_entry kMainSymbols[];
extern const std::size_t kMainSymbolCount;
