/** @file overlay.cpp — scl::overlay: the libraries are linked with the firmware, into one
 *  DRAM window, and a load copies one in. So the last one loaded is the only one resident.
 *  Here the float library and the SD library time-share the window. */
#include "lyra.hpp"
#include "overlay/overlay.h"
#include "floatlib.h"
#include <cstdint>
#include <cstring>

extern "C" {
extern std::uint8_t _ovl_vma[], _ovfloat_lma[], _ovfloat_size[], _ovsd_lma[], _ovsd_size[];
void sd_init(void);
std::uint32_t sd_selftest(std::uint32_t seed, std::uint32_t len);
std::uint32_t sd_transfers(std::uint32_t a, std::uint32_t b);
void sd_discard(void);
}

bool lib_load(Lib& lib, unsigned, const char* base) {
    static scl_overlay_module mods[2];
    static scl_overlay_region region;
    static bool ready;
    if (!ready) {
        mods[0] = {_ovl_vma, _ovfloat_lma, reinterpret_cast<std::uint32_t>(_ovfloat_size), "ovfloat"};
        mods[1] = {_ovl_vma, _ovsd_lma, reinterpret_cast<std::uint32_t>(_ovsd_size), "ovsd"};
        scl_overlay_init(&region, mods, 2);
        ready = true;
    }
    lib.slot = std::strcmp(base, "sd") == 0 ? 1u : 0u;
    return scl_overlay_load(&region, static_cast<int>(lib.slot)) == 0;
}

void lib_unload(Lib&) {}   // the next load overwrites the window

const char* lib_first_missing() { return nullptr; }   // the linker checked

void* lib_sym(const Lib&, const char* name) {
#define OV(n) if (std::strcmp(name, #n) == 0) return reinterpret_cast<void*>(&n)
    OV(float_add); OV(float_mul);
    OV(sd_init); OV(sd_selftest); OV(sd_transfers); OV(sd_discard);
    return nullptr;   // ovfloat has no `calls` counter
}
