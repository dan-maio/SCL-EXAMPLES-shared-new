/** @file xip-dynamic-nolib.cpp — scl::xip-dynamic-nolib: an r10 .so runs in place from flash, with its
 *  data in RAM, behind a POSIX-style dlopen. Calls into main look up at call time. */
#include "pizero.hpp"
#include "xip/dynamic-nolib/adapter.hpp"
#include <cstdio>

static void* g_mods[4];

bool lib_load(Lib& lib, unsigned slot, const char* base) {
    char file[32];
    std::snprintf(file, sizeof file, "%s.so", base);
    auto f = read_host_file(file);
    std::span<std::byte> image{const_cast<std::byte*>(f.data()), f.size()};

    xip::nolib::set_symtab(kMainSymbols, kMainSymbolCount);
    const auto addr = flash_address(slot);
    xip::nolib::patch_main_imports(image, addr);           // before programming: NOR can't be redone
    auto in_flash = write_to_flash(image, addr);
    if (in_flash.empty()) return false;

    g_mods[slot] = xip::nolib::dlopen(in_flash, base);
    if (!g_mods[slot]) return false;
    lib.slot = slot;
    lib.got = xip::nolib::get_got_base(g_mods[slot]);
    return true;
}
void lib_unload(Lib& lib) { xip::nolib::dlclose(g_mods[lib.slot]); g_mods[lib.slot] = nullptr; }
const char* lib_first_missing() { return xip::nolib::first_missing_import(); }
void* lib_sym(const Lib& lib, const char* name) { return reinterpret_cast<void*>(xip::nolib::dlsym(g_mods[lib.slot], name)); }
