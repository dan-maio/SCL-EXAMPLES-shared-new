/**
 * @file main.cpp — pizero/static: scl::static copies a plain -fPIC .so whole into RAM.
 * No r10, no loader code after this function returns.
 */
#include "pizero.hpp"
#include "elf-reader.h"
#include "module.h"

extern "C" { extern const std::uint32_t _reloc[10]; const std::uint32_t _reloc[10] = {}; }  // main has none

const char* const kExampleName = "static";

bool example_load(FloatLib& lib) {
    auto image = read_host_file("floatlib.pic.so");

    static elf_loader::symbol_table syms;
    syms.add_entries(kMainSymbols, kMainSymbolCount);

    static scl::Module m;
    if (!m.load_buf(reinterpret_cast<const uint8_t*>(image.data()), image.size(),
                    static_cast<uint8_t*>(example_arena()), kArenaBytes, &syms))
        return false;
    if (m.first_missing_import()) return false;   // an unresolved import would fault when called

    lib.add = m.sym<float_fn>("float_add");
    lib.mul = m.sym<float_fn>("float_mul");
    lib.scale = m.sym<float_fn>("float_scale");
    return lib.add && lib.mul && lib.scale;
}
