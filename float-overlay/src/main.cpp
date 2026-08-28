/**
 * @file main.cpp — float-overlay: two modules time-share one SRAM window (scl::overlay).
 *
 * The overlay technique (LOADER-CHOICE.md §3e): ovadd (float_add) and ovmul
 * (float_mul) are built WITH the firmware and bound at LINK time to the SAME
 * SRAM window; their bytes sit back-to-back in flash. "Loading" a module is just
 * scl_overlay_load() — a memcpy from its flash LMA into the window + DSB/ISB.
 * Only one is resident at a time, so main loads the right one before each call,
 * then calls it directly by name. Output on USART1 (PA9, 115200) + semihosting.
 * WeAct F411CE / QEMU netduinoplus2.
 */
#include "board.h"
#include "semihost.h"
#include "uart.h"
#include <cstdint>
extern "C" {
#include "overlay/overlay.h"           // scl_overlay_init / scl_overlay_load
}

/* the overlay-resident exports (bound to the window at link time) */
extern "C" float ov_add(float, float);
extern "C" float ov_mul(float, float);

/* linker-emitted descriptors: each module's flash LMA + byte size, and the window VMA */
extern std::uint8_t _ovadd_lma[], _ovadd_size[], _ovmul_lma[], _ovmul_size[], _ovl_vma[];

enum { OV_ADD = 0, OV_MUL = 1 };
static scl_overlay_module g_mods[2];
static scl_overlay_region g_ovl;

static void line(const char* s) {
    uart_puts(s);                      // USART1 PA9 115200 (bare-metal)
    trace_puts(s); trace_putc('\n');  // semihosting console
}
/* float -> "int.frac" (3 decimals), no printf-float */
static char* utoa(unsigned v, char* p){ char t[10]; int i=0;
    if(!v){*p++='0';return p;} while(v){t[i++]=char('0'+v%10);v/=10;} while(i)*p++=t[--i]; return p; }
static char* ftoa(float v, char* p){ if(v<0){*p++='-';v=-v;}
    unsigned ip=(unsigned)v, fp=(unsigned)((v-(float)ip)*1000.0f+0.5f);
    p=utoa(ip,p); *p++='.'; if(fp<100)*p++='0'; if(fp<10)*p++='0'; return utoa(fp,p); }

extern "C" int main(void) {
    board_init();
    uart_init();
    line("float-overlay: two modules time-share one SRAM window (scl::overlay)");

    // build the descriptor table from the linker symbols (VMA is shared; LMA/size per module)
    g_mods[OV_ADD] = { _ovl_vma, _ovadd_lma, (std::uint32_t)(std::uintptr_t)_ovadd_size, "ovadd" };
    g_mods[OV_MUL] = { _ovl_vma, _ovmul_lma, (std::uint32_t)(std::uintptr_t)_ovmul_size, "ovmul" };
    scl_overlay_init(&g_ovl, g_mods, 2);

    // load ovadd into the window, then call float_add directly
    scl_overlay_load(&g_ovl, OV_ADD);
    float sum = ov_add(2.5f, 3.5f);          // 6.000

    // load ovmul (overwrites the window), then call float_mul directly
    scl_overlay_load(&g_ovl, OV_MUL);
    float prod = ov_mul(2.5f, 3.5f);         // 8.750

    char l[80], s[16];
    char* e = l; for (const char* p = "  ov_add(2.500, 3.500) = "; *p;) *e++ = *p++;
    *ftoa(sum, s) = 0; for (char* p = s; *p;) *e++ = *p++; *e = 0; line(l);
    e = l; for (const char* p = "  ov_mul(2.500, 3.500) = "; *p;) *e++ = *p++;
    *ftoa(prod, s) = 0; for (char* p = s; *p;) *e++ = *p++; *e = 0; line(l);

    line("RESULT: SUCCESS");
    semihost_exit(0);
    for (;;) { }
}
