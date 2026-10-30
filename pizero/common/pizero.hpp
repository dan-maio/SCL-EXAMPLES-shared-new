/**
 * @file pizero.hpp
 * @brief What every Pi-Zero example shares, so that each main.cpp shows only its loader.
 *
 * An example implements example_load(). The common code calls it at boot, on core 0,
 * with core 1 still in the bootrom and the kernel not started. It then starts SMP, and
 * calls the library from both cores.
 */
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

#include "scl/symtab-register.h"

/// The library's functions, wherever the loader put them.
using float_fn = float (*)(float, float);
struct FloatLib {
    float_fn add = nullptr;
    float_fn mul = nullptr;      ///< May stay null: the overlay example unloads it.
    float_fn scale = nullptr;    ///< a * b * app_gain(), which it imports from main.
    std::uint32_t got = 0;       ///< r10 for the r10 models, else 0.
};

/// Each example defines these two.
extern const char* const kExampleName;
bool example_load(FloatLib& lib);

// ---- helpers for example_load() -------------------------------------------------------
void say(const char* s);                       ///< To semihosting and the UART.
std::span<const std::byte> read_host_file(const char* name);  ///< From the build's fixtures.
std::span<const std::byte> write_to_flash(std::span<std::byte> image, std::uint32_t flash_addr);
std::uint32_t flash_address(unsigned slot);    ///< Where slot's image will live in flash.
void* example_arena();                         ///< 64 KB of RAM to load into.
inline constexpr std::size_t kArenaBytes = 64u * 1024u;  // see arena.S

/// main's exports: what the library may import. Hand to the loader as a symbol table.
extern const scl_sym_entry kMainSymbols[];
extern const std::size_t kMainSymbolCount;
