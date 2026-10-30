/**
 * @file cppeh.cpp — the WeAct tests' libcppeh, ehx_catch and libcppsh: C++ exceptions thrown and
 * caught inside a library, across libraries and into main, RTTI, and virtual classes.
 * libstdc++ here is single-threaded, so these calls run on one core at a time.
 */
#include "pizero.hpp"
#include "cross-error.hpp"
#include <cxxabi.h>
#include <exception>

const char* const kAppName = "cppeh-" LOADER_NAME;

static Lib g_libs[3];
static Fn eh_selftest, eh_throw, eh_dyncast, ehx_catch_cross, cppsh_make, cppsh_compute, cppsh_instances, cppsh_free;
static std::uint32_t g_widget_at_boot;

[[noreturn]] static void on_terminate() {
    say("  FAIL: std::terminate\nRESULT: FAIL\n");
    for (;;) {}
}

bool app_load() {
    std::set_terminate(on_terminate);
    static const char* const set[] = {"libcppeh", "ehx_catch"};
    if (!lib_load_set(g_libs, set, 2) || !lib_load(g_libs[2], 2, "libcppsh") || !libs_ready()) return false;
    eh_selftest = lib_fn(g_libs[0], "eh_selftest"); eh_throw = lib_fn(g_libs[0], "eh_throw"); eh_dyncast = lib_fn(g_libs[0], "eh_dyncast");
    ehx_catch_cross = lib_fn(g_libs[1], "ehx_catch_cross");
    cppsh_make = lib_fn(g_libs[2], "cppsh_make"); cppsh_compute = lib_fn(g_libs[2], "cppsh_compute");
    cppsh_instances = lib_fn(g_libs[2], "cppsh_instances"); cppsh_free = lib_fn(g_libs[2], "cppsh_free");
    const std::uint32_t w = call(cppsh_make, 7);
    g_widget_at_boot = call(cppsh_compute, w, 5);
    call(cppsh_free, w);
    return true;
}

bool app_start() { return true; }

void app_on_core(unsigned) {
    show("eh_dyncast()", call(eh_dyncast), 2u);
    show("ehx_catch_cross(0)", call(ehx_catch_cross, 0), 0u);
    show("eh_selftest(1) [throw and catch inside libcppeh]", call(eh_selftest, 1), 98u);
    show("ehx_catch_cross(1) [throw in libcppeh, catch in ehx_catch]", call(ehx_catch_cross, 1), 1007u);
    std::uint32_t caught = 0;
    try {
        call(eh_throw, 1);
    } catch (const cross_error& e) {
        caught = static_cast<std::uint32_t>(e.code());
    }
    show("eh_throw(1) [throw in libcppeh, catch in main], code", caught, 7u);
    const std::uint32_t before = call(cppsh_instances);
    const std::uint32_t w = call(cppsh_make, 7);
    show("cppsh_compute(w,5) [virtual call]", call(cppsh_compute, w, 5), g_widget_at_boot);
    show("cppsh_instances() after make", call(cppsh_instances), before + 1u);
    call(cppsh_free, w);
    show("cppsh_instances() after free [counts objects created]", call(cppsh_instances), before + 1u);
}
