/**
 * @file pizero.hpp
 * @brief What every Pi-Zero example shares, so that an app shows only what it does.
 *
 * Boot order, on every example:
 *   app_load()      core 0, before the kernel, core 1 in the bootrom: load the libraries
 *   app_start()     core 0, kernel running, core 1 still in the bootrom: anything that
 *                   writes flash, because core 1 must not be running while XIP is off
 *   app_on_core(n)  on core 0, then on core 1, with SMP running
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

// ---- one loaded library: loaders/<loader>.cpp implements these two ----------------------
struct Lib {
    unsigned slot = 0;       ///< Which of the app's libraries: 0 or 1.
    std::uint32_t got = 0;   ///< r10 for the r10 models, else 0.
};
bool lib_load(Lib& lib, unsigned slot, const char* base);   ///< base is "floatlib" or "spifs".
void* lib_sym(const Lib& lib, const char* name);

// Enter a library with r10 = its GOT base, whichever loader it came from.
int lib_call(const Lib& lib, void* fn, std::uintptr_t a = 0, std::uintptr_t b = 0, std::uintptr_t c = 0, std::uintptr_t d = 0);
float lib_callf(const Lib& lib, void* fn, float a, float b);

// ---- helpers ------------------------------------------------------------------------
void say(const char* s);                       ///< To semihosting and the UART.
void sayf(const char* fmt, unsigned a = 0, unsigned b = 0, const char* s = "");
void expect(bool ok, const char* what);        ///< A failure makes the example fail.
std::span<const std::byte> read_host_file(const char* name);
std::span<const std::byte> write_to_flash(std::span<std::byte> image, std::uint32_t flash_addr);
std::uint32_t flash_address(unsigned slot);    ///< Where slot's image will live in flash.
std::uint8_t* example_arena(unsigned slot);    ///< RAM to load slot into.
inline constexpr std::size_t kArenaBytes = 32u * 1024u;  // each slot's share of arena.S

/// main's exports: what the libraries may import.
extern const scl_sym_entry kMainSymbols[];
extern const std::size_t kMainSymbolCount;
