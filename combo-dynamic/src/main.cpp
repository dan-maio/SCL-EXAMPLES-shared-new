/**
 * @file main.cpp — combo-dynamic: float module -> unload -> spi module, all via
 *        the POSIX dlopen API (RAM / scl::dynamic-newlib, Model E).
 *
 * Sequential: dlopen floatlib.so, compute sum/prod, KEEP them in main, dlclose
 * it; then dlopen spifs.so, write the kept values to a file on the on-board
 * W25Q64 flat-fs and read them back. dlclose journals a byte-exact revert, so
 * main's imports are restored between the two modules. Output on USART1 (PA9,
 * 115200) + the semihosting console. WeAct F411CE.
 */
#include <cstdio>
#include "stm32f4xx_hal.h"
#include "uart.h"
#include "firmware/dl-impl.hpp"

/* the standard POSIX API, backed by the SCL dl:: facade */
extern "C" {
void*       dlopen (const char* p, int)     { return dl::dlopen(p); }
void*       dlsym  (void* h, const char* n) { return dl::dlsym((dl::handle)h, n); }
int         dlclose(void* h)                { return dl::dlclose((dl::handle)h); }
const char* dlerror(void)                   { return dl::dlerror(); }
}

#ifndef SCL_SO_DIR
#define SCL_SO_DIR "."
#endif

static void line(const char* s) { uart_puts(s); puts(s); }
static void fstr(float v, char* p) {          /* float -> "int.frac" (3 dp) */
    if (v < 0) { *p++ = '-'; v = -v; }
    unsigned ip = (unsigned)v, fp = (unsigned)((v - (float)ip) * 1000.0f + 0.5f);
    char* q = p; int n = 0; unsigned t = ip; do { ++n; t /= 10; } while (t);
    q += n; *q-- = 0; t = ip; do { *q-- = char('0' + t % 10); t /= 10; } while (t);
    p += n; *p++ = '.'; *p++ = char('0' + fp/100%10); *p++ = char('0' + fp/10%10);
    *p++ = char('0' + fp%10); *p = 0;
}

int main(void) {
    HAL_Init();
    uart_init();
    line("combo-dynamic: float -> unload -> spi (POSIX dlopen, RAM)");

    /* ===== 1) dlopen the FLOAT module, compute, keep the values ===== */
    float sum = 0, prod = 0;
    {
        void* h = dlopen(SCL_SO_DIR "/floatlib.so", 0);
        if (!h) { line("  float dlopen failed"); line("RESULT: FAILURE"); for (;;) {} }
        auto add = (float (*)(float,float))dlsym(h, "float_add");
        auto mul = (float (*)(float,float))dlsym(h, "float_mul");
        sum  = add(2.5f, 3.5f);          /* 6.000 — kept in main */
        prod = mul(2.5f, 3.5f);          /* 8.750 */
        char sa[16], sm[16], l[80];
        fstr(sum, sa); fstr(prod, sm);
        std::snprintf(l, sizeof l, "  floatlib: sum=%s prod=%s", sa, sm); line(l);
        dlclose(h);                      /* ===== 2) unload float (journaled revert) ===== */
        line("  float module unloaded (main restored)");
    }

    /* ===== 3) dlopen the SPI module, persist the kept values ===== */
    {
        void* h = dlopen(SCL_SO_DIR "/spifs.so", 0);
        if (!h) { line("  spi dlopen failed"); line("RESULT: FAILURE"); for (;;) {} }
        auto spifs_init   = (int (*)(void))dlsym(h, "spifs_init");
        auto spifs_format = (int (*)(void))dlsym(h, "spifs_format");
        auto spifs_create = (int (*)(const char*, const void*, unsigned))dlsym(h, "spifs_create");
        auto spifs_read   = (int (*)(const char*, void*, unsigned, unsigned*))dlsym(h, "spifs_read");
        if (spifs_init() != 0) { line("  W25Q64 NOT found -- check SPI wiring"); for (;;) {} }
        line("  spifs.so loaded; W25Q64 up");
        spifs_format();

        char sa[16], sm[16], body[64]; fstr(sum, sa); fstr(prod, sm);
        int k = std::snprintf(body, sizeof body, "sum=%s prod=%s\n", sa, sm);
        spifs_create("calc.txt", body, (unsigned)k);
        line("  wrote calc.txt to the SPI flatfs");

        char buf[64]; unsigned got = 0;
        if (spifs_read("calc.txt", buf, sizeof buf - 1, &got) == 0) {
            if (got >= sizeof buf) got = sizeof buf - 1; buf[got] = 0;
            for (unsigned j = 0; j < got; ++j) if (buf[j] == '\n') buf[j] = 0;
            char l[96]; std::snprintf(l, sizeof l, "  read back calc.txt: \"%s\"", buf); line(l);
        }
        dlclose(h);
    }

    line("combo-dynamic: done");
    line("RESULT: SUCCESS");
    for (;;) { }
}
