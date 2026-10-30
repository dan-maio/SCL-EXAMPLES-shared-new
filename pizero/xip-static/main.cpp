/**
 * @file main.cpp — pizero/xip-static: scl::xip-static runs an r10 .so in place from flash,
 * with its data in RAM. main's imports are bound before flashing, so no lookup at call time.
 */
#include "pizero.hpp"
#include "xip/static/adapter.hpp"

const char* const kExampleName = "xip-static";

bool example_load(FloatLib& lib) {
    auto file = read_host_file("floatlib.so");
    std::span<std::byte> image{const_cast<std::byte*>(file.data()), file.size()};

    xip::stat::set_symtab(kMainSymbols, kMainSymbolCount);
    const auto addr = flash_address(0);
    xip::stat::bind_host_imports(image, addr);          // before programming: NOR can't be redone
    auto in_flash = write_to_flash(image, addr);
    if (in_flash.empty()) return false;

    static xip::stat::SModule m;
    if (!xip::stat::load(in_flash, m) || xip::stat::first_missing_import()) return false;

    lib.add = reinterpret_cast<float_fn>(xip::stat::resolve(m, "float_add"));
    lib.mul = reinterpret_cast<float_fn>(xip::stat::resolve(m, "float_mul"));
    lib.scale = reinterpret_cast<float_fn>(xip::stat::resolve(m, "float_scale"));
    lib.got = m.got_base;
    return lib.add && lib.mul && lib.scale;
}
