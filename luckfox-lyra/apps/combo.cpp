/**
 * @file combo.cpp — the WeAct combo example on the Luckfox Lyra: load floatlib and compute,
 * unload it, load the SD library into the same memory, save the results on the card, then
 * read them back on all three cores. The values survive because they live in main.
 */
#include "sd-lib.hpp"
#include <cstdio>
#include <cstring>

const char* const kAppName = "combo-" LOADER_NAME;

static float g_sum, g_prod;
static Sd g_sd;
static char g_body[64];

static std::uintptr_t g_float_at, g_sd_at;
static bool g_float_gone;

bool app_load() {
    Lib flt;
    if (!lib_load(flt, 0, "floatlib")) return false;
    void* add = lib_sym(flt, "float_add");
    void* mul = lib_sym(flt, "float_mul");
    if (!add || !mul) return false;
    g_sum = lib_callf(flt, add, 2.5f, 3.5f);     // 6.000: kept in main
    g_prod = lib_callf(flt, mul, 2.5f, 3.5f);    // 8.750
    g_float_at = reinterpret_cast<std::uintptr_t>(add);
    lib_unload(flt);

    if (!g_sd.load(0) || !libs_ready()) return false;
    g_sd_at = reinterpret_cast<std::uintptr_t>(g_sd.init_);
    g_float_gone = lib_sym(g_sd.lib, "float_add") == nullptr;
    return true;
}

bool app_start() {
    const unsigned s = static_cast<unsigned>(g_sum * 1000.0f + 0.5f), p = static_cast<unsigned>(g_prod * 1000.0f + 0.5f);
    std::snprintf(g_body, sizeof g_body, "sum=%u.%03u prod=%u.%03u\n", s / 1000u, s % 1000u, p / 1000u, p % 1000u);
    say("  floatlib: "); say(g_body);
    sayf("  floatlib was at 0x%08x; unloaded; sd is at 0x%08x\n", static_cast<unsigned>(g_float_at), static_cast<unsigned>(g_sd_at));
    if (std::strcmp(LOADER_NAME, "overlay") != 0) expect(g_float_gone, "floatlib is gone after the unload");
    expect(s == 6000u && p == 8750u, "floatlib computed the right values");
    if (!board_sd_begin()) { say("  SD card NOT available\n"); return false; }
    g_sd.init();
    const unsigned len = static_cast<unsigned>(std::strlen(g_body));
    if (!board_sd_store(g_body, len)) { say("  SD write FAILED\n"); return false; }
    char back[64];
    expect(board_sd_load(back, len) != 0 && std::memcmp(back, g_body, len) == 0, "the body reads back from the SD card");
    say("  wrote sclsd.bin to the SD card\n");
    return true;
}

void app_on_core(unsigned) {
    char back[64];
    const unsigned len = static_cast<unsigned>(std::strlen(g_body));
    const bool ok = board_sd_load(back, len) != 0;
    expect(ok, "file reads back");
    if (!ok) return;
    back[len < sizeof back ? len : sizeof back - 1] = 0;
    say("    sclsd.bin: "); say(back);
    expect(std::memcmp(back, g_body, len) == 0, "file holds what was written");
}
