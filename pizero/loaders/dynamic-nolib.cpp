/** @file dynamic-nolib.cpp — scl::dynamic-nolib: a freestanding dlopen of an r10 .so into
 *  RAM. Calls into main look the target up at call time, on both cores. */
#include "pizero.hpp"
#include "dynamic-nolib/dl.hpp"
#include "loader/sources/memory-byte-source.hpp"
#include <cstdio>

static fw::handle* g_mods[2];

bool lib_load(Lib& lib, unsigned slot, const char* base) {
    char file[32];
    std::snprintf(file, sizeof file, "%s.so", base);
    auto image = read_host_file(file);

    fw::dl_set_symtab(kMainSymbols, kMainSymbolCount);
    fw::dl_set_lazy_binding(false);
    fw::dl_enable_cross_call(true);
    fw::dl_cross_trace(0);

    loader::memory_byte_source src{image};
    auto m = fw::dlopen_source(src, base);
    if (!m || fw::dl_first_missing_import()) return false;
    g_mods[slot] = *m;
    lib.slot = slot;
    lib.got = (*m)->got_base;
    return true;
}
void* lib_sym(const Lib& lib, const char* name) {
    auto v = fw::dlsym(*g_mods[lib.slot], name);
    return v ? reinterpret_cast<void*>(*v) : nullptr;
}
