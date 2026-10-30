/**
 * @file ovfix.cpp — the WeAct overlay test's ov1, ov2 and ovcpp, linked with the firmware into one
 * SRAM window. Swapping is a load, so it happens at boot on core 0; ovcpp then stays resident.
 */
#include "pizero.hpp"
#include "overlay/overlay.h"

extern "C" {
extern std::uint8_t _ovl_vma[], _ov1_lma[], _ov1_size[], _ov2_lma[], _ov2_size[], _ovcpp_lma[], _ovcpp_size[];
__attribute__((long_call)) std::uint32_t ov1_use_fm(void);
__attribute__((long_call)) std::uint32_t ov1_count(void);
__attribute__((long_call)) std::uint32_t ov2_sum(void);
__attribute__((long_call)) std::uint32_t ovcpp_method(std::uint32_t, std::uint32_t);
__attribute__((long_call)) std::uint32_t ovcpp_fcompute(std::uint32_t, std::uint32_t);
__attribute__((long_call)) std::uint32_t ovcpp_heap(std::uint32_t);
}

const char* const kAppName = "ovfix-overlay";

// This app swaps the window itself, so it has no loaders/*.cpp.
bool lib_load(Lib&, unsigned, const char*) { return false; }
void* lib_sym(const Lib&, const char*) { return nullptr; }
const char* lib_first_missing() { return nullptr; }

static std::uint32_t g_method_at_boot, g_f_at_boot;
static std::uint32_t g_fm, g_count_1, g_sum, g_count_2;

bool app_load() {
    static scl_overlay_module mods[3];
    static scl_overlay_region region;
    mods[0] = {_ovl_vma, _ov1_lma, reinterpret_cast<std::uint32_t>(_ov1_size), "ov1"};
    mods[1] = {_ovl_vma, _ov2_lma, reinterpret_cast<std::uint32_t>(_ov2_size), "ov2"};
    mods[2] = {_ovl_vma, _ovcpp_lma, reinterpret_cast<std::uint32_t>(_ovcpp_size), "ovcpp"};
    scl_overlay_init(&region, mods, 3);

    if (scl_overlay_load(&region, 0) != 0) return false;
    g_fm = ov1_use_fm();
    g_count_1 = ov1_count();
    if (scl_overlay_load(&region, 1) != 0) return false;
    g_sum = ov2_sum();
    if (scl_overlay_load(&region, 0) != 0) return false;   // restored from flash: its counter restarts
    g_count_2 = ov1_count();
    if (scl_overlay_load(&region, 2) != 0) return false;   // stays resident from here on
    g_method_at_boot = ovcpp_method(7, 5);
    g_f_at_boot = ovcpp_fcompute(7, 5);
    return true;
}

bool app_start() {
    say("  at boot, on core 0, swapping the window:\n");
    show("ov1_use_fm()", g_fm, 123u + 0xF1u + 7u + 0xC1u);
    show("ov2_sum()", g_sum, 0x24C0u);
    show("ov1_count() after the swap back", g_count_2, 1u);
    return true;
}

void app_on_core(unsigned) {
    show("ovcpp_method(7,5)", ovcpp_method(7, 5), g_method_at_boot);
    show("ovcpp_fcompute(7,5)", ovcpp_fcompute(7, 5), g_f_at_boot);
    app_lock();     // ovcpp_heap calls malloc directly
    const std::uint32_t h = ovcpp_heap(8);
    app_unlock();
    show("ovcpp_heap(8)", h, 533u);
}
