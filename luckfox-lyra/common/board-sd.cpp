/**
 * @file board-sd.cpp
 * @brief The Lyra's storage: the microSD card, mounted through FatFs.
 *
 * Linked only by the SD apps (sd, combo, drivers), and the XIP/overlay forms of
 * them. The card is the board's boot card: mount the existing FAT volume and keep
 * everything under tests/, exactly as the Lyra test suite does.
 */
#include "lyra.hpp"

#include <fatfs_hw.hpp>
#include <sd.hpp>

namespace {
sd::SdCard g_card;
bool g_ready;
}  // namespace

bool board_sd_begin() {
    if (g_ready) return true;
    if (!g_card.init()) {
        say("  SD card init failed: ");
        say(g_card.last_error());
        say("\n");
        return false;
    }
    fatfshw::bind_card(g_card);
    if (!fatfshw::mount_volume() || !fatfshw::ensure_tests_dir()) {
        say("  SD volume: ");
        say(fatfshw::last_error());
        say("\n");
        return false;
    }
    g_ready = true;
    return true;
}

extern "C" int board_sd_store(const void* data, std::uint32_t len) {
    return fatfshw::store_file("sclsd.bin", data, len) ? 1 : 0;
}
extern "C" int board_sd_load(void* data, std::uint32_t len) {
    return fatfshw::load_file("sclsd.bin", data, len) ? 1 : 0;
}
