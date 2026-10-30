/**
 * @file combo.cpp — the WeAct combo example on the Pi-Zero: load floatlib and compute, load
 * the filesystem and save the results in a file, then read the file back on both cores.
 * With scl::overlay the second load overwrites the first, as on the WeAct.
 */
#include "spifs-lib.hpp"
#include <cstdio>

const char* const kAppName = "combo-" LOADER_NAME;

static float g_sum, g_prod;
static Spifs g_fs;
static char g_body[64];

bool app_load() {
    Lib flt;
    if (!lib_load(flt, 0, "floatlib")) return false;
    void* add = lib_sym(flt, "float_add");
    void* mul = lib_sym(flt, "float_mul");
    if (!add || !mul) return false;
    g_sum = lib_callf(flt, add, 2.5f, 3.5f);     // 6.000: kept in main
    g_prod = lib_callf(flt, mul, 2.5f, 3.5f);    // 8.750
    return g_fs.load(1);
}

bool app_start() {
    const unsigned s = static_cast<unsigned>(g_sum * 1000.0f + 0.5f), p = static_cast<unsigned>(g_prod * 1000.0f + 0.5f);
    std::snprintf(g_body, sizeof g_body, "sum=%u.%03u prod=%u.%03u\n", s / 1000u, s % 1000u, p / 1000u, p % 1000u);
    say("  floatlib: "); say(g_body);
    expect(s == 6000u && p == 8750u, "floatlib computed the right values");
    if (g_fs.init() != 0) { say("  flash NOT found\n"); return false; }
    if (g_fs.format() != 0) { say("  format FAILED\n"); return false; }
    g_fs.create("calc.txt", g_body, static_cast<unsigned>(__builtin_strlen(g_body)));
    say("  wrote calc.txt to the flatfs\n");
    return true;
}

void app_on_core(unsigned) { g_fs.cat("calc.txt", "sum=6.000 prod=8.750\n"); }
