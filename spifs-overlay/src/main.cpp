/**
 * @file main.cpp — spifs-overlay: the W25Q64 SPI-flatfs driver runs from an
 *        overlay window (scl::overlay).
 *
 * The whole driver (w25q64 + spi-shim + fs-archive-spi + spifs-api) is ONE
 * overlay module, bound at LINK time to a shared SRAM window and stored in flash.
 * "Loading" it is scl_overlay_load() — a memcpy into the window. The driver then
 * runs from RAM and calls main's STM32 HAL (HAL_SPI_*) bound at link time
 * (-fno-pic -mlong-calls); no relocation, no dlsym. main drives the flat-fs by
 * name: init / format / create / list / read. USART1 PA9 115200 + semihosting.
 * WeAct F411CE.
 */
#include "board.h"
#include "semihost.h"
#include "uart.h"
#include "stm32f4xx_hal.h"
#include <cstdint>
extern "C" {
#include "overlay/overlay.h"
}

/* the flat-fs exports, resident in the overlay window (bound at link time) */
extern "C" {
int spifs_init(void);
int spifs_format(void);
int spifs_create(const char* name, const void* data, unsigned len);
unsigned spifs_count(void);
int spifs_list(unsigned idx, char* name_out, unsigned* size_out);
int spifs_read(const char* name, void* buf, unsigned cap, unsigned* out_len);
}

/* linker descriptors for the single driver overlay */
extern std::uint8_t _ovspifs_lma[], _ovspifs_size[], _ovl_vma[];

static scl_overlay_module g_mods[1];
static scl_overlay_region g_ovl;

static void line(const char* s) { uart_puts(s); trace_puts(s); trace_putc('\n'); }
static char* utoa(unsigned v, char* p){ char t[10]; int i=0;
    if(!v){*p++='0';return p;} while(v){t[i++]=char('0'+v%10);v/=10;} while(i)*p++=t[--i]; return p; }

extern "C" int main(void) {
    board_init();
    uart_init();
    HAL_Init();                                     // start the HAL tick for the SPI driver
    line("spifs-overlay: W25Q64 flat-fs driver runs from an overlay window (scl::overlay)");

    // one overlay module = the whole driver; load it into the window
    g_mods[0] = { _ovl_vma, _ovspifs_lma, (std::uint32_t)(std::uintptr_t)_ovspifs_size, "spifs" };
    scl_overlay_init(&g_ovl, g_mods, 1);
    scl_overlay_load(&g_ovl, 0);                    // memcpy driver into the window
    line("  driver loaded into the overlay window");

    if (spifs_init() != 0)  { line("  W25Q64 NOT found -- check SPI wiring"); semihost_exit(1); }
    line("  W25Q64 detected, SPI up");
    if (spifs_format() != 0){ line("  format FAILED"); semihost_exit(1); }
    line("  flatfs formatted");

    spifs_create("hello.txt", "Hello WeAct!\n", 13);
    spifs_create("nums.txt",  "1 2 3 4 5\n",    10);
    spifs_create("scl.txt",   "loaded by name\n", 15);
    { char l[64]; int k=0; for(const char* s="  created 3 files; count=";*s;)l[k++]=*s++;
      char* e=utoa(spifs_count(),l+k); *e=0; line(l); }

    line("  -- ls --");
    for (unsigned i=0;; ++i) {
        char name[17]; unsigned fsz=0;
        if (spifs_list(i,name,&fsz)!=0) break;
        char l[64]; int k=0; l[k++]=' ';l[k++]=' ';l[k++]=' ';l[k++]=' ';
        for(char* s=name;*s;)l[k++]=*s++; l[k++]=' ';l[k++]='(';
        char* e=utoa(fsz,l+k); k=e-l; l[k++]='B';l[k++]=')'; l[k]=0; line(l);
    }

    line("  -- cat --");
    const char* files[3] = { "hello.txt", "nums.txt", "scl.txt" };
    for (int f=0; f<3; ++f) {
        char buf[96]; unsigned got=0;
        if (spifs_read(files[f],buf,sizeof(buf)-1,&got)==0) {
            if (got>=sizeof buf) got=sizeof buf-1; buf[got]=0;
            for (unsigned j=0;j<got;++j) if(buf[j]=='\n') buf[j]=0;
            char l[160]; int k=0; l[k++]=' ';l[k++]=' ';l[k++]=' ';l[k++]=' ';
            for(const char* s=files[f];*s;)l[k++]=*s++; l[k++]=':';l[k++]=' ';l[k++]='"';
            for(char* s=buf;*s;)l[k++]=*s++; l[k++]='"'; l[k]=0; line(l);
        }
    }

    line("spifs-overlay: done");
    line("RESULT: SUCCESS");
    semihost_exit(0);
    for (;;) { }
}
