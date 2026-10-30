/**
 * @file main.cpp — pizero/reloc: scl::reloc loads an ET_REL object into RAM.
 * No GOT, no r10, no loader code after this function returns.
 */
#include "pizero.hpp"
#include "reloc/reloc-loader.hpp"
#include "scl/symtab-runtime.hpp"

const char* const kExampleName = "reloc";

bool example_load(FloatLib& lib) {
    auto image = read_host_file("floatlib.rel.o");

    // main's symbols, as a table the loader can search.
    alignas(4) static std::byte blob[128];
    static scl::symtab::view table;
    const auto at = reinterpret_cast<std::uint32_t>(blob);
    table = scl::symtab::view(blob, scl::symtab::build_blob(kMainSymbols, kMainSymbolCount, blob, sizeof blob, at), at);

    auto m = reloc::load(image, {static_cast<std::byte*>(example_arena()), kArenaBytes},
                         scl::symtab::source(table));
    if (!m) return false;

    lib.add = reinterpret_cast<float_fn>(reloc::sym(*m, "float_add"));
    lib.mul = reinterpret_cast<float_fn>(reloc::sym(*m, "float_mul"));
    lib.scale = reinterpret_cast<float_fn>(reloc::sym(*m, "float_scale"));
    return lib.add && lib.mul && lib.scale;
}
