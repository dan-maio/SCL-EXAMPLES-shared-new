/** @file spifs-api.cpp — the loadable fixture's exported C ABI.
 *  Thin wrapper over the W25Q64 driver (spi-shim) + the SPI flatfs. */
#include "spifs.h"
#include "fs-archive-spi.hpp"
#include "spi-flash.h"
#include <cstring>
using namespace fw;

extern "C" {
int spifs_init(void) {
    spi_flash_init();                              // power up SPI + W25Q (HAL from main)
    std::uint8_t mid = 0, tid = 0;
    return (spi_flash_read_id(&mid, &tid) == 0 && mid == 0xEF) ? 0 : -1;
}
int spifs_format(void)                                   { return fs_format() ? 0 : -1; }
int spifs_create(const char* n, const void* d, unsigned l){ return fs_add(n, d, l) ? 0 : -1; }
int spifs_delete(const char* n)                          { return fs_remove(n) ? 0 : -1; }
unsigned spifs_count(void)                               { return fs_count(); }
int spifs_list(unsigned idx, char* name_out, unsigned* size_out) {
    const FsEntry* e = fs_entry(idx);
    if (!e) return -1;
    std::memcpy(name_out, e->name, kFsNameMax);
    name_out[kFsNameMax] = 0;                      // caller buffer is kFsNameMax+1
    if (size_out) *size_out = e->size;
    return 0;
}
int spifs_read(const char* n, void* buf, unsigned cap, unsigned* out_len) {
    const FsEntry* e = fs_find(n);
    if (!e) return -1;
    unsigned len = e->size < cap ? e->size : cap;
    spi_flash_read(e->off, static_cast<std::uint8_t*>(buf), static_cast<std::uint16_t>(len));
    if (out_len) *out_len = e->size;
    return 0;
}
}
