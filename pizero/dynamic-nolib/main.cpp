/**
 * @file main.cpp — pizero/dynamic-nolib: scl::dynamic-nolib, a freestanding dlopen, loads
 * an r10 .so into RAM. Calls into main look the target up at call time, on both cores.
 */
#include "pizero.hpp"
#include "dynamic-nolib/dl.hpp"
#include "loader/sources/memory-byte-source.hpp"

const char* const kExampleName = "dynamic-nolib";

bool example_load(FloatLib& lib) {
    auto image = read_host_file("floatlib.so");

    fw::dl_set_symtab(kMainSymbols, kMainSymbolCount);
    fw::dl_set_lazy_binding(false);
    fw::dl_enable_cross_call(true);
    fw::dl_cross_trace(0);

    loader::memory_byte_source src{image};
    auto m = fw::dlopen_source(src, "floatlib");
    if (!m || fw::dl_first_missing_import()) return false;

    auto sym = [&](const char* n) {
        auto v = fw::dlsym(**m, n);
        return v ? reinterpret_cast<float_fn>(*v) : nullptr;
    };
    lib.add = sym("float_add");
    lib.mul = sym("float_mul");
    lib.scale = sym("float_scale");
    lib.got = (*m)->got_base;
    return lib.add && lib.mul && lib.scale;
}
