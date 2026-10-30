/** @file dynamic-newlib.cpp — scl::dynamic-newlib: a plain -fPIC .so, onto the heap. No r10,
 *  and no loader code runs after the load. */
#include "pizero.hpp"
#include "dynamic-newlib/dynamic-linker.hpp"
#include "dynamic-newlib/link-context.hpp"
#include "loader/sources/memory-byte-source.hpp"
#include <cstdio>

extern "C" { extern const std::uint32_t _reloc[10]; const std::uint32_t _reloc[10] = {}; }  // main has none

static firmware::link_context g_ctx;
static firmware::library* g_mods[4];

static firmware::dynamic_linker& linker() {
    static firmware::dynamic_linker l(g_ctx);
    return l;
}

bool lib_load(Lib& lib, unsigned slot, const char* base) {
    char file[32];
    std::snprintf(file, sizeof file, "%s.pic.so", base);
    auto image = read_host_file(file);

    g_ctx.set_symtab(reinterpret_cast<const firmware::sym_entry*>(kMainSymbols), kMainSymbolCount);
    loader::memory_byte_source src{image};
    auto m = linker().load_from_source(src);
    if (!m) return false;
    g_mods[slot] = *m;
    lib.slot = slot;
    return true;
}
void lib_unload(Lib& lib) { linker().unload(g_mods[lib.slot]); g_mods[lib.slot] = nullptr; }
const char* lib_first_missing() { return linker().first_missing_import(); }
void* lib_sym(const Lib& lib, const char* name) {
    auto v = linker().lookup(g_mods[lib.slot], name);
    return v ? reinterpret_cast<void*>(*v) : nullptr;
}
