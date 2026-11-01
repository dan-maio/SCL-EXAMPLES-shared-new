/**
 * @file sd.cpp — the WeAct spifs example on the Luckfox Lyra: the board has no application
 * flash, so the SD card is the storage. Load the SD library and its driver, init the card
 * (it needs the kernel's clock, so that happens in app_start), then write a pattern and read
 * it back on every core.
 */
#include "sd-lib.hpp"

const char* const kAppName = "sd-" LOADER_NAME;

static Sd g_sd;

bool app_load() { return g_sd.load(0) && libs_ready(); }

bool app_start() {
    if (!board_sd_begin()) {
        say("  SD card NOT available\n");
        return false;
    }
    say("  SD card mounted\n");
    g_sd.init();
    expect(g_sd.selftest(0xA5, 64) == 0u, "SD card write and read back, on core 0");
    sayf("  SD transfers so far: %u\n", g_sd.transfers());
    return true;
}

void app_on_core(unsigned) {
    show("sd_selftest(0x5A, 32) [write, read back on the SD card], mismatches", g_sd.selftest(0x5A, 32), 0u);
    show("sd_transfers()", g_sd.transfers());
}
