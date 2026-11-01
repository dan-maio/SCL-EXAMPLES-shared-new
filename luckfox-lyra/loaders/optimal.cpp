/** @file optimal.cpp — scl::optimal: an image linked at fixed addresses. Plain pointers: no
 *  GOT, no r10, and no loader code runs after the load. */
#include "lyra.hpp"
#include "optimal/loader.hpp"
#include "scl/symtab-runtime.hpp"
#include <cstdio>
#include <new>

static optimal::Module g_mods[4];

bool lib_load(Lib& lib, unsigned slot, const char* base) {
    char file[32];
    std::snprintf(file, sizeof file, "%s.opt.so", base);
    auto image = read_host_file(file);

    alignas(4) static std::byte blob[2048];
    static scl::symtab::view table;
    const auto at = reinterpret_cast<std::uint32_t>(blob);
    table = scl::symtab::view(blob, scl::symtab::build_blob(kMainSymbols, kMainSymbolCount, blob, sizeof blob, at), at);
    xip::nopic::set_main_symbols(scl::symtab::source(table));

    if (!g_mods[slot].load(image)) return false;
    lib.slot = slot;
    return true;
}
void lib_unload(Lib& lib) {   // the destructor deregisters it and restores main
    g_mods[lib.slot].~Module();
    new (&g_mods[lib.slot]) optimal::Module;
}
const char* lib_first_missing() {
    xip::nopic::resolve_all();   // the libraries that import from each other
    return xip::nopic::first_missing_import();
}
void* lib_sym(const Lib& lib, const char* name) { return reinterpret_cast<void*>(g_mods[lib.slot].sym(name)); }
