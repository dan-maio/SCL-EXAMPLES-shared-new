/** @file reloc.cpp — scl::reloc: an ET_REL object, linked into RAM. No GOT, no r10, and no
 *  loader code runs after the load. */
#include "pizero.hpp"
#include "reloc/reloc-loader.hpp"
#include "scl/symtab-runtime.hpp"
#include <cstdio>

static reloc::module g_mods[2];

bool lib_load(Lib& lib, unsigned slot, const char* base) {
    char file[32];
    std::snprintf(file, sizeof file, "%s.rel.o", base);
    auto image = read_host_file(file);

    alignas(4) static std::byte blob[256];
    static scl::symtab::view table;
    const auto at = reinterpret_cast<std::uint32_t>(blob);
    table = scl::symtab::view(blob, scl::symtab::build_blob(kMainSymbols, kMainSymbolCount, blob, sizeof blob, at), at);

    auto m = reloc::load(image, {reinterpret_cast<std::byte*>(example_arena(slot)), kArenaBytes},
                         scl::symtab::source(table));
    if (!m) return false;
    g_mods[slot] = *m;
    lib.slot = slot;
    return true;
}
void* lib_sym(const Lib& lib, const char* name) {
    return reinterpret_cast<void*>(reloc::sym(g_mods[lib.slot], name));
}
