/** @file overlay.cpp — scl::overlay: the libraries are linked with the firmware, into one SRAM
 *  window, and a load copies one in. So the last one loaded is the only one resident. */
#include "pizero.hpp"
#include "overlay/overlay.h"
#include "floatlib.h"
#include "spifs.h"
#include <cstring>

extern "C" {
extern std::uint8_t _ovl_vma[], _ovfloat_lma[], _ovfloat_size[], _ovspifs_lma[], _ovspifs_size[];
}

bool lib_load(Lib& lib, unsigned, const char* base) {
    static scl_overlay_module mods[2];
    static scl_overlay_region region;
    static bool ready;
    if (!ready) {
        mods[0] = {_ovl_vma, _ovfloat_lma, reinterpret_cast<std::uint32_t>(_ovfloat_size), "ovfloat"};
        mods[1] = {_ovl_vma, _ovspifs_lma, reinterpret_cast<std::uint32_t>(_ovspifs_size), "ovspifs"};
        scl_overlay_init(&region, mods, 2);
        ready = true;
    }
    lib.slot = std::strcmp(base, "spifs") == 0 ? 1u : 0u;
    return scl_overlay_load(&region, static_cast<int>(lib.slot)) == 0;
}

void* lib_sym(const Lib&, const char* name) {
#define OV(n) if (std::strcmp(name, #n) == 0) return reinterpret_cast<void*>(&n)
    OV(float_add); OV(float_mul);
    OV(spifs_init); OV(spifs_format); OV(spifs_create); OV(spifs_delete);
    OV(spifs_count); OV(spifs_list); OV(spifs_read);
    return nullptr;   // ovfloat has no `calls` counter
}
