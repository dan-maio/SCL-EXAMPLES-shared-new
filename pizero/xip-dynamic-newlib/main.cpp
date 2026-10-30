/**
 * @file main.cpp — pizero/xip-dynamic-newlib: scl::xip-dynamic-newlib runs an r10 .so in place from flash,
 * with its data in RAM, behind a POSIX-style dlopen. Calls into main look up at call time.
 */
#include "pizero.hpp"
#include "xip/dynamic-newlib/adapter.hpp"
extern "C" { extern const std::uint32_t _reloc[10]; const std::uint32_t _reloc[10] = {}; }  // main has none

const char* const kExampleName = "xip-dynamic-newlib";

bool example_load(FloatLib& lib) {
    auto file = read_host_file("floatlib.so");
    std::span<std::byte> image{const_cast<std::byte*>(file.data()), file.size()};

    xip::newlib::set_symtab(kMainSymbols, kMainSymbolCount);
    const auto addr = flash_address(0);
    xip::newlib::patch_main_imports(image, addr);           // before programming: NOR can't be redone
    auto in_flash = write_to_flash(image, addr);
    if (in_flash.empty()) return false;

    void* h = xip::newlib::dlopen(in_flash, "floatlib");
    if (!h || xip::newlib::first_missing_import()) return false;

    lib.add = reinterpret_cast<float_fn>(xip::newlib::dlsym(h, "float_add"));
    lib.mul = reinterpret_cast<float_fn>(xip::newlib::dlsym(h, "float_mul"));
    lib.scale = reinterpret_cast<float_fn>(xip::newlib::dlsym(h, "float_scale"));
    lib.got = xip::newlib::get_got_base(h);
    return lib.add && lib.mul && lib.scale;
}
