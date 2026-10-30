/**
 * @file main.cpp — pizero/xip-dynamic-nolib: scl::xip-dynamic-nolib runs an r10 .so in place from flash,
 * with its data in RAM, behind a POSIX-style dlopen. Calls into main look up at call time.
 */
#include "pizero.hpp"
#include "xip/dynamic-nolib/adapter.hpp"


const char* const kExampleName = "xip-dynamic-nolib";

bool example_load(FloatLib& lib) {
    auto file = read_host_file("floatlib.so");
    std::span<std::byte> image{const_cast<std::byte*>(file.data()), file.size()};

    xip::nolib::set_symtab(kMainSymbols, kMainSymbolCount);
    const auto addr = flash_address(0);
    xip::nolib::patch_main_imports(image, addr);           // before programming: NOR can't be redone
    auto in_flash = write_to_flash(image, addr);
    if (in_flash.empty()) return false;

    void* h = xip::nolib::dlopen(in_flash, "floatlib");
    if (!h || xip::nolib::first_missing_import()) return false;

    lib.add = reinterpret_cast<float_fn>(xip::nolib::dlsym(h, "float_add"));
    lib.mul = reinterpret_cast<float_fn>(xip::nolib::dlsym(h, "float_mul"));
    lib.scale = reinterpret_cast<float_fn>(xip::nolib::dlsym(h, "float_scale"));
    lib.got = xip::nolib::get_got_base(h);
    return lib.add && lib.mul && lib.scale;
}
