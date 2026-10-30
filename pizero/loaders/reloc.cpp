/** @file reloc.cpp — scl::reloc: an ET_REL object, linked into RAM. No GOT, no r10, and no
 *  loader code runs after the load. */
#include "pizero.hpp"
#include "reloc/reloc-loader.hpp"
#include "scl/symtab-runtime.hpp"
#include <cstdio>

static reloc::module g_mods[4];
static reloc::module_set g_set;
static unsigned g_set_count;

static scl::symtab::view& main_table() {
    alignas(4) static std::byte blob[2048];
    static scl::symtab::view table;
    const auto at = reinterpret_cast<std::uint32_t>(blob);
    table = scl::symtab::view(blob, scl::symtab::build_blob(kMainSymbols, kMainSymbolCount, blob, sizeof blob, at), at);
    return table;
}

/* Libraries that import from each other link together, so their calls are direct. */
bool lib_load_set(Lib* libs, const char* const* bases, unsigned n) {
    static std::span<const std::byte> images[4];
    for (unsigned i = 0; i < n; ++i) {
        char file[32];
        std::snprintf(file, sizeof file, "%s.rel.o", bases[i]);
        images[i] = read_host_file(file);
        libs[i].slot = i;
    }
    auto set = reloc::load_set(std::span<const std::span<const std::byte>>(images, n),
                               {reinterpret_cast<std::byte*>(example_arena(0)), n * kArenaBytes},
                               scl::symtab::source(main_table()));
    if (!set) return false;
    g_set = *set;
    g_set_count = n;
    return true;
}

bool lib_load(Lib& lib, unsigned slot, const char* base) {
    char file[32];
    std::snprintf(file, sizeof file, "%s.rel.o", base);
    auto image = read_host_file(file);

    auto m = reloc::load(image, {reinterpret_cast<std::byte*>(example_arena(slot)), kArenaBytes},
                         scl::symtab::source(main_table()));
    if (!m) return false;
    g_mods[slot] = *m;
    lib.slot = slot;
    return true;
}
const char* lib_first_missing() { return nullptr; }   // a missing import fails the load
void* lib_sym(const Lib& lib, const char* name) {
    if (lib.slot < g_set_count) return reinterpret_cast<void*>(reloc::sym(g_set, name));
    return reinterpret_cast<void*>(reloc::sym(g_mods[lib.slot], name));
}
