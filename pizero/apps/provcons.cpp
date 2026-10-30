/**
 * @file provcons.cpp — the WeAct tests' libprov, libcons and mylib2: one library importing from
 * another, and a library that imports functions and data from main.
 */
#include "pizero.hpp"

const char* const kAppName = "provcons-" LOADER_NAME;

static Lib g_libs[3];
static Fn prov_value, cons_call, lf, lf2, lf3, xxx;

bool app_load() {
    static const char* const set[] = {"libprov", "libcons"};
    if (!lib_load_set(g_libs, set, 2) || !lib_load(g_libs[2], 2, "mylib2") || !libs_ready()) return false;
    prov_value = lib_fn(g_libs[0], "prov_value");
    cons_call = lib_fn(g_libs[1], "cons_call");
    lf = lib_fn(g_libs[2], "lf"); lf2 = lib_fn(g_libs[2], "lf2"); lf3 = lib_fn(g_libs[2], "lf3"); xxx = lib_fn(g_libs[2], "xxx");
    return true;
}

bool app_start() { return true; }

void app_on_core(unsigned) {
    show("prov_value(5)", call(prov_value, 5), 105u);
    show("cons_call(5) [libcons calls libprov]", call(cons_call, 5), 106u);
    show("lf(7)", call(lf, 7), 30u);
    show("lf2(7)", call(lf2, 7), 23u);
    show("lf3(7) [calls w() in main]", call(lf3, 7), 2u * (30u + 23u + 27u));
    show("xxx()", call(xxx));
}
