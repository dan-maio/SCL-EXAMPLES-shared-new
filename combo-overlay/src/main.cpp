/**
 * @file main.cpp — combo-overlay: two modules time-share one SRAM window (scl::overlay).
 *
 * Load the FLOAT module into the window, compute sum/prod, KEEP them in main;
 * then load the SPIFS driver — which OVERWRITES the same window — and persist the
 * kept values to a file on the on-board W25Q64 flat-fs, read it back. The values
 * survive the swap because they live in main, not the module. "Loading" a module
 * is scl_overlay_load() — a memcpy into the window. USART1 PA9 115200 + semihosting.
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

/* exports resident in the window (bound at link time; valid only when the owning
 * module is the one currently loaded) */
extern "C" {
float float_add(float, float);
float float_mul(float, float);
int   spifs_init(void);
int   spifs_format(void);
int   spifs_create(const char* name, const void* data, unsigned len);
int   spifs_read(const char* name, void* buf, unsigned cap, unsigned* out_len);
}

extern std::uint8_t _ovfloat_lma[], _ovfloat_size[],
                    _ovspifs_lma[], _ovspifs_size[], _ovl_vma[];

enum { OV_FLOAT = 0, OV_SPIFS = 1 };
static scl_overlay_module g_mods[2];
static scl_overlay_region g_ovl;

static void line(const char* s) { uart_puts(s); trace_puts(s); trace_putc('\n'); }
static char* utoa(unsigned v, char* p){ char t[10]; int i=0;
    if(!v){*p++='0';return p;} while(v){t[i++]=char('0'+v%10);v/=10;} while(i)*p++=t[--i]; return p; }
static char* ftoa(float v, char* p){ if(v<0){*p++='-';v=-v;}
    unsigned ip=(unsigned)v, fp=(unsigned)((v-(float)ip)*1000.0f+0.5f);
    p=utoa(ip,p); *p++='.'; if(fp<100)*p++='0'; if(fp<10)*p++='0'; return utoa(fp,p); }

extern "C" int main(void) {
    board_init();
    uart_init();
    HAL_Init();
    line("combo-overlay: float -> swap window -> spifs (scl::overlay)");

    g_mods[OV_FLOAT] = { _ovl_vma, _ovfloat_lma, (std::uint32_t)(std::uintptr_t)_ovfloat_size, "ovfloat" };
    g_mods[OV_SPIFS] = { _ovl_vma, _ovspifs_lma, (std::uint32_t)(std::uintptr_t)_ovspifs_size, "ovspifs" };
    scl_overlay_init(&g_ovl, g_mods, 2);

    /* ===== 1) FLOAT module in the window: compute, KEEP the values ===== */
    scl_overlay_load(&g_ovl, OV_FLOAT);
    float sum  = float_add(2.5f, 3.5f);          // 6.000  (kept in main)
    float prod = float_mul(2.5f, 3.5f);          // 8.750
    { char l[80], sa[16], sm[16]; *ftoa(sum,sa)=0; *ftoa(prod,sm)=0;
      int k=0; for(const char* s="  floatlib: sum=";*s;)l[k++]=*s++;
      for(char* s=sa;*s;)l[k++]=*s++; for(const char* s=" prod=";*s;)l[k++]=*s++;
      for(char* s=sm;*s;)l[k++]=*s++; l[k]=0; line(l); }

    /* ===== 2) swap: SPIFS driver overwrites the window; persist the values ===== */
    scl_overlay_load(&g_ovl, OV_SPIFS);
    line("  window swapped: spifs driver resident");
    if (spifs_init() != 0)  { line("  W25Q64 NOT found -- check SPI wiring"); semihost_exit(1); }
    line("  W25Q64 up");
    spifs_format();

    char body[64]; int k=0; char sa[16], sm[16]; *ftoa(sum,sa)=0; *ftoa(prod,sm)=0;
    for(const char* s="sum=";*s;)body[k++]=*s++; for(char* s=sa;*s;)body[k++]=*s++;
    for(const char* s=" prod=";*s;)body[k++]=*s++; for(char* s=sm;*s;)body[k++]=*s++;
    body[k++]='\n';
    spifs_create("calc.txt", body, (unsigned)k);
    line("  wrote calc.txt to the W25Q64 flatfs");

    char buf[64]; unsigned got=0;
    if (spifs_read("calc.txt", buf, sizeof buf-1, &got) == 0) {
        if (got>=sizeof buf) got=sizeof buf-1; buf[got]=0;
        for (unsigned j=0;j<got;++j) if(buf[j]=='\n') buf[j]=0;
        char l[96]; int m=0; for(const char* s="  read back calc.txt: \"";*s;)l[m++]=*s++;
        for(char* s=buf;*s;)l[m++]=*s++; l[m++]='"'; l[m]=0; line(l);
    }

    line("combo-overlay: done");
    line("RESULT: SUCCESS");
    semihost_exit(0);
    for (;;) { }
}
