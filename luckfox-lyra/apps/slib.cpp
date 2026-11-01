/**
 * @file slib.cpp — the WeAct tests' mylib1 and slib: a constructor that runs at load, internal
 * calls, and an import from main with its data.
 */
#include "lyra.hpp"

const char* const kAppName = "slib-" LOADER_NAME;

static Lib g_libs[2];
static Fn library_function, s_add, s_scale, s_via_helper, s_apply, s_use_host, s_get_a, s_get_b, s_print, s_text;

bool app_load() {
    static const char* const set[] = {"mylib1"};
    if (!lib_load_set(g_libs, set, 1) || !lib_load(g_libs[1], 1, "slib") || !libs_ready()) return false;
    library_function = lib_fn(g_libs[0], "library_function");
    s_add = lib_fn(g_libs[1], "s_add"); s_scale = lib_fn(g_libs[1], "s_scale"); s_via_helper = lib_fn(g_libs[1], "s_via_helper");
    s_apply = lib_fn(g_libs[1], "s_apply"); s_use_host = lib_fn(g_libs[1], "s_use_host");
    s_get_a = lib_fn(g_libs[1], "s_get_a"); s_get_b = lib_fn(g_libs[1], "s_get_b");
    s_print = lib_fn(g_libs[1], "s_print"); s_text = lib_fn(g_libs[1], "s_text");
    return true;
}

bool app_start() { return true; }

void app_on_core(unsigned) {
    show("s_get_a() [set by its constructor]", call(s_get_a), 15u);
    show("s_get_b() [set by its constructor]", call(s_get_b), 30u);
    show("s_add(1,2)", call(s_add, 1, 2), 18u);
    show("s_scale(2)", call(s_scale, 2), 84u);
    show("s_via_helper(1)", call(s_via_helper, 1), 118u);
    show("s_apply(2)", call(s_apply, 2), 85u);
    show("s_use_host(1) [calls host_f in main]", call(s_use_host, 1), 1003u);
    say("  s_text() = \""); say(reinterpret_cast<const char*>(call(s_text))); say("\"\n");
    call(s_print);
    show("library_function(39)", call(library_function, 39), 62u);
}
