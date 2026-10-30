/** @file w25q64-rp2350.cpp — the w25q64 class on the Pi-Zero, in place of the WeAct's HAL
 *  driver. The board has no W25Q64, so it uses a 1 MB part of its own QSPI flash. Writing
 *  stops execute-in-place, so main does the writes, from SRAM; reads come through XIP. */
#include "w25q64.h"
#include <cstring>

extern "C" {
int  board_flash_read_id(uint8_t* mid, uint8_t* tid);
void board_flash_erase_sector(uint32_t addr);
int  board_flash_program(uint32_t addr, const uint8_t* data, uint32_t len);
const uint8_t* board_flash_map(uint32_t addr);
}

w25q64::w25q64() {}
void w25q64::power_up() {}
/* spifs_init() in the shared spifs-api.cpp accepts only Winbond's 0xEF. This board's chip is a
 * Puya (85 20 18), so a good ID from the chip is reported to it as that value. */
bool w25q64::read_id(uint8_t* mid, uint8_t* tid) {
    if (board_flash_read_id(mid, tid) == 0) return false;
    *mid = 0xEF;
    return true;
}
void w25q64::sector_erase(uint32_t addr) { board_flash_erase_sector(addr); }
bool w25q64::page_program(uint32_t addr, const uint8_t* data, uint16_t len) { return board_flash_program(addr, data, len) != 0; }
bool w25q64::write(uint32_t addr, const uint8_t* data, uint32_t len) { return board_flash_program(addr, data, len) != 0; }
void w25q64::read(uint32_t addr, uint8_t* data, uint32_t len) { std::memcpy(data, board_flash_map(addr), len); }
