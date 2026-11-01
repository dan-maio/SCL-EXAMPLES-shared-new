/**
 * @file cpp.cpp — the WeAct tests' cpp3, c2, c1 and mylib1: C++ classes, recursion, heap and a
 * float call, with libraries calling each other in a cycle, and imports from main.
 * Not on scl::static, which has no cyclic imports.
 */
#include "lyra.hpp"
#include <cstring>

const char* const kAppName = "cpp-" LOADER_NAME;

static Lib g_libs[4];
static Fn method, recurse, float_cross, heap, c2_add, c2_param, c2_state, c1_call_c2, c1_use_fm, c1_state, library_function, retstr;
static std::uint32_t g_method_at_boot;

bool app_load() {
    static const char* const set[] = {"cpp3", "c2", "c1"};
    if (!lib_load_set(g_libs, set, 3) || !lib_load(g_libs[3], 3, "mylib1") || !libs_ready()) return false;
    method = lib_fn(g_libs[0], "cpp3_method"); recurse = lib_fn(g_libs[0], "cpp3_recurse");
    float_cross = lib_fn(g_libs[0], "cpp3_float_cross"); heap = lib_fn(g_libs[0], "cpp3_heap");
    c2_add = lib_fn(g_libs[1], "c2_add"); c2_param = lib_fn(g_libs[1], "c2_param"); c2_state = lib_fn(g_libs[1], "c2_state");
    c1_call_c2 = lib_fn(g_libs[2], "c1_call_c2"); c1_use_fm = lib_fn(g_libs[2], "c1_use_fm"); c1_state = lib_fn(g_libs[2], "c1_state");
    library_function = lib_fn(g_libs[3], "library_function"); retstr = lib_fn(g_libs[3], "retstr");
    g_method_at_boot = call(method, 7, 5);
    return true;
}

bool app_start() { return true; }

void app_on_core(unsigned) {
    show("cpp3_method(7,5)", call(method, 7, 5), g_method_at_boot);
    show("c1_call_c2(7)", call(c1_call_c2, 7));
    show("c2_add(10,20)", call(c2_add, 10, 20));
    show("c2_param()", call(c2_param), 0x24C0u);
    show("c1_use_fm()", call(c1_use_fm), 123u + 0xF1u + 7u + 0xC1u);
    show("cpp3_heap(8)", call(heap, 8), 533u);
    show("cpp3_recurse(4)", call(recurse, 4), 10u);
    show("cpp3_float_cross(10)", call(float_cross, 10), 10u + 0x2200u);
    show("library_function(39)", call(library_function, 39), 62u);
    const char* s = reinterpret_cast<const char*>(call(retstr));
    say("  retstr() = \""); say(s); say("\"\n");
    expect(std::strstr(s, "mylib1") != nullptr, "retstr");
    show("c2_state()", call(c2_state));
    show("c1_state()", call(c1_state));
}
