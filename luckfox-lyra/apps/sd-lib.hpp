/** @file sd-lib.hpp — the SD library's API, through whichever loader loaded it.
 *  On the Lyra the SD card takes the place of the WeAct SPI flash. */
#pragma once
#include "lyra.hpp"
#include <cstdint>

struct Sd {
    Lib lib;
    void* init_ = nullptr; void* selftest_ = nullptr; void* transfers_ = nullptr; void* discard_ = nullptr;

    bool load(unsigned slot) {
        if (!lib_load(lib, slot, "sd")) return false;
        init_ = lib_sym(lib, "sd_init");
        selftest_ = lib_sym(lib, "sd_selftest");
        transfers_ = lib_sym(lib, "sd_transfers");
        discard_ = lib_sym(lib, "sd_discard");
        return init_ && selftest_ && transfers_ && discard_;
    }
    void init() { lib_call(lib, init_); }
    /// Write a pattern from seed, read it back, and return how many bytes differ.
    unsigned selftest(unsigned seed, unsigned len) {
        return static_cast<unsigned>(lib_call(lib, selftest_, seed, len));
    }
    unsigned transfers() { return static_cast<unsigned>(lib_call(lib, transfers_)); }
    void discard() { lib_call(lib, discard_); }
};
