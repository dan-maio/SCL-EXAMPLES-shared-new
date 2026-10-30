/**
 * @file main.cpp — pizero/dynamic-newlib: scl::dynamic-newlib loads a plain -fPIC .so
 * onto the heap. No r10, no loader code after this function returns.
 */
#include "pizero.hpp"
#include "dynamic-newlib/dynamic-linker.hpp"
#include "dynamic-newlib/link-context.hpp"
#include "loader/sources/memory-byte-source.hpp"

extern "C" { extern const std::uint32_t _reloc[10]; const std::uint32_t _reloc[10] = {}; }  // main has none

const char* const kExampleName = "dynamic-newlib";

bool example_load(FloatLib& lib) {
    auto image = read_host_file("floatlib.pic.so");

    static firmware::link_context ctx;
    ctx.set_symtab(reinterpret_cast<const firmware::sym_entry*>(kMainSymbols), kMainSymbolCount);
    static firmware::dynamic_linker linker(ctx);

    loader::memory_byte_source src{image};
    auto m = linker.load_from_source(src);
    if (!m || linker.first_missing_import()) return false;

    auto sym = [&](const char* n) {
        auto v = linker.lookup(*m, n);
        return v ? reinterpret_cast<float_fn>(*v) : nullptr;
    };
    lib.add = sym("float_add");
    lib.mul = sym("float_mul");
    lib.scale = sym("float_scale");
    return lib.add && lib.mul && lib.scale;
}
