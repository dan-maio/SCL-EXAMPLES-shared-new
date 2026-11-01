/** @file xip-static.cpp — scl::xip-static: an r10 .so runs in place from the DRAM image
 *  region, with its data in RAM. main's imports are bound before the copy, so no lookup at
 *  call time. */
#include "lyra.hpp"
#include "xip/static/adapter.hpp"
#include <cstdio>

static xip::stat::SModule g_mods[4];

bool lib_load(Lib& lib, unsigned slot, const char* base) {
    char file[32];
    std::snprintf(file, sizeof file, "%s.so", base);
    auto f = read_host_file(file);
    std::span<std::byte> image{const_cast<std::byte*>(f.data()), f.size()};

    xip::stat::set_symtab(kMainSymbols, kMainSymbolCount);
    const auto addr = flash_address(slot);
    xip::stat::bind_host_imports(image, addr);          // before the copy
    auto in_flash = write_to_flash(image, addr);
    if (in_flash.empty()) return false;

    if (!xip::stat::load(in_flash, g_mods[slot])) return false;
    lib.slot = slot;
    lib.got = g_mods[slot].got_base;
    return true;
}
void lib_unload(Lib& lib) { xip::stat::unload(g_mods[lib.slot]); }
const char* lib_first_missing() { return xip::stat::first_missing_import(); }
void* lib_sym(const Lib& lib, const char* name) {
    return reinterpret_cast<void*>(xip::stat::resolve(g_mods[lib.slot], name));
}
