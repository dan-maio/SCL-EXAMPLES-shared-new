/**
 * @file main.cpp — pizero/optimal: scl::optimal loads an image linked at fixed addresses.
 * Plain function pointers: no GOT, no r10, no loader code after this function returns.
 */
#include "pizero.hpp"
#include "optimal/loader.hpp"
#include "scl/symtab-runtime.hpp"

const char* const kExampleName = "optimal";

bool example_load(FloatLib& lib) {
    auto image = read_host_file("floatlib.opt.so");

    alignas(4) static std::byte blob[128];
    static scl::symtab::view table;
    const auto at = reinterpret_cast<std::uint32_t>(blob);
    table = scl::symtab::view(blob, scl::symtab::build_blob(kMainSymbols, kMainSymbolCount, blob, sizeof blob, at), at);
    xip::nopic::set_main_symbols(scl::symtab::source(table));

    static optimal::Module m;
    if (!m.load(image)) return false;
    xip::nopic::resolve_all();
    if (xip::nopic::first_missing_import()) return false;

    lib.add = reinterpret_cast<float_fn>(m.sym("float_add"));
    lib.mul = reinterpret_cast<float_fn>(m.sym("float_mul"));
    lib.scale = reinterpret_cast<float_fn>(m.sym("float_scale"));
    return lib.add && lib.mul && lib.scale;
}
