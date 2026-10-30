/**
 * @file float.cpp — the WeAct float example on the Pi-Zero: load floatlib, a hard-float
 * add/multiply library whose constructor must run at load, and call it from both cores.
 * The `calls` counter lives in the library's data, so both cores must see it grow.
 */
#include "pizero.hpp"

const char* const kAppName = "float-" LOADER_NAME;

static Lib g_lib;
static void* g_add, *g_mul;
static unsigned* g_calls;

bool app_load() {
    if (!lib_load(g_lib, 0, "floatlib")) return false;
    g_add = lib_sym(g_lib, "float_add");
    g_mul = lib_sym(g_lib, "float_mul");
    g_calls = static_cast<unsigned*>(lib_sym(g_lib, "calls"));   // the overlay's copy has none
    return g_add && g_mul;
}

bool app_start() { return true; }

static void show(const char* what, float got, float want) {
    const unsigned milli = static_cast<unsigned>(got * 1000.0f + 0.5f);
    say("  "); say(what); sayf(" = %u.%03u\n", milli / 1000u, milli % 1000u);
    expect(got == want, what);
}

void app_on_core(unsigned core) {
    show("float_add(2.5, 3.5)", lib_callf(g_lib, g_add, 2.5f, 3.5f), 6.0f);
    show("float_mul(2.5, 3.5)", lib_callf(g_lib, g_mul, 2.5f, 3.5f), 8.75f);
    if (g_calls) {
        sayf("  floatlib calls = %u\n", *g_calls);
        expect(*g_calls == 2u * (core + 1u), "calls counts both cores' calls");
    }
}
