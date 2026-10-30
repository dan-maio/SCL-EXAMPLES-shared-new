/** @file static.cpp — scl::static: a plain -fPIC .so, copied whole into RAM. No r10, and no
 *  loader code runs after the load. */
#include "pizero.hpp"
#include "elf-reader.h"
#include "module.h"
#include <cstdio>

extern "C" { extern const std::uint32_t _reloc[10]; const std::uint32_t _reloc[10] = {}; }  // main has none

static scl::Module g_mods[2];

bool lib_load(Lib& lib, unsigned slot, const char* base) {
    char file[32];
    std::snprintf(file, sizeof file, "%s.pic.so", base);
    auto image = read_host_file(file);

    static elf_loader::symbol_table syms;
    static bool added;
    if (!added) syms.add_entries(kMainSymbols, kMainSymbolCount);
    added = true;

    scl::Module& m = g_mods[slot];
    if (!m.load_buf(reinterpret_cast<const uint8_t*>(image.data()), image.size(), example_arena(slot), kArenaBytes, &syms))
        return false;
    if (m.first_missing_import()) return false;   // an unresolved import would fault when called
    lib.slot = slot;
    return true;
}
void* lib_sym(const Lib& lib, const char* name) { return g_mods[lib.slot].sym<void*>(name); }
