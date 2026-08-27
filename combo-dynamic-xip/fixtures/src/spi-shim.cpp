/** @file spi-shim.cpp — spi-flash.h C API backed by the HAL w25q64 driver.
 *  Lives IN the loadable fixture so the flatfs's spi_flash_* calls resolve here.
 *  HAL_Init() is done by main (the firmware) before the module runs. */
#include "w25q64.h"
#include "spi-flash.h"
static w25q64 flash;
extern "C" {
void spi_flash_init(void)                    { flash.power_up(); }
int  spi_flash_read_id(uint8_t* m, uint8_t* t){ return flash.read_id(m, t) ? 0 : -1; }
void spi_flash_sector_erase(uint32_t a)      { flash.sector_erase(a); }
int  spi_flash_page_program(uint32_t a, const uint8_t* d, uint16_t n){ return flash.page_program(a, d, n) ? 0 : -1; }
void spi_flash_read(uint32_t a, uint8_t* d, uint16_t n){ flash.read(a, d, n); }
int  spi_flash_write(uint32_t a, const uint8_t* d, uint32_t n){ return flash.write(a, d, n) ? 0 : -1; }
void spi_flash_get_sr(uint32_t* sr, uint32_t* cr1){ (void)sr; (void)cr1; }
}
