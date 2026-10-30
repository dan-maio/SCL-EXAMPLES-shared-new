/**
 * @file main.cpp — pizero/overlay: scl::overlay swaps modules into one SRAM window.
 * Modules are linked with the firmware. Swapping is the load, so it stays on core 0 at boot:
 * float_mul runs once from its overlay, then the other overlay stays resident for SMP.
 */
#include "pizero.hpp"
#include "overlay/overlay.h"

extern "C" {
extern std::uint8_t _ovl_vma[], _ov1_lma[], _ov1_size[], _ov2_lma[], _ov2_size[];
__attribute__((long_call)) float float_add(float, float);
__attribute__((long_call)) float float_scale(float, float);
__attribute__((long_call)) float float_mul(float, float);
}

const char* const kExampleName = "overlay";

bool example_load(FloatLib& lib) {
    static scl_overlay_module mods[2];
    mods[0] = {_ovl_vma, _ov1_lma, reinterpret_cast<std::uint32_t>(_ov1_size), "ov1"};
    mods[1] = {_ovl_vma, _ov2_lma, reinterpret_cast<std::uint32_t>(_ov2_size), "ov2"};
    static scl_overlay_region region;
    scl_overlay_init(&region, mods, 2);

    if (scl_overlay_load(&region, 1) != 0) return false;
    if (float_mul(2.5f, 3.5f) != 8.75f) return false;
    say("  ov2: float_mul(2.5, 3.5) = 8.750, on core 0 at boot\n");

    if (scl_overlay_load(&region, 0) != 0) return false;   // stays resident from here on
    lib.add = float_add;
    lib.scale = float_scale;
    return true;
}
