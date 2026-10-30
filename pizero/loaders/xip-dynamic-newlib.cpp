/** @file xip-dynamic-newlib.cpp — scl::xip-dynamic-newlib: an r10 .so runs in place from flash, with its
 *  data in RAM, behind a POSIX-style dlopen. Calls into main look up at call time. */
#include "pizero.hpp"
#include "xip/dynamic-newlib/adapter.hpp"
#include <cstdio>

extern "C" { extern const std::uint32_t _reloc[10]; const std::uint32_t _reloc[10] = {}; }  // main has none
static void* g_mods[2];

bool lib_load(Lib& lib, unsigned slot, const char* base) {
    char file[32];
    std::snprintf(file, sizeof file, "%s.so", base);
    auto f = read_host_file(file);
    std::span<std::byte> image{const_cast<std::byte*>(f.data()), f.size()};

    xip::newlib::set_symtab(kMainSymbols, kMainSymbolCount);
    const auto addr = flash_address(slot);
    xip::newlib::patch_main_imports(image, addr);           // before programming: NOR can't be redone
    auto in_flash = write_to_flash(image, addr);
    if (in_flash.empty()) return false;

    g_mods[slot] = xip::newlib::dlopen(in_flash, base);
    if (!g_mods[slot] || xip::newlib::first_missing_import()) return false;
    lib.slot = slot;
    lib.got = xip::newlib::get_got_base(g_mods[slot]);
    return true;
}
void* lib_sym(const Lib& lib, const char* name) { return reinterpret_cast<void*>(xip::newlib::dlsym(g_mods[lib.slot], name)); }
