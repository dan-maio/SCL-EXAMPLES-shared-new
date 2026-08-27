/**
 * @file main.cpp — spifs-dynamic: drive the on-board W25Q64 flat-fs via a module
 *        loaded with the POSIX dlopen API.
 *
 * Model E (import_in_project_full.md): scl::dynamic-newlib, hosted POSIX
 * dlopen/dlsym/dlclose. The module (spifs.so, plain -fPIC) carries the W25Q64
 * SPI driver + a flat-fs and imports the STM32 HAL from main (resolved via the
 * embedded symtab). Open, resolve the fs entry points by name, run a
 * format/create/list/read/delete cycle. Output on USART1 (PA9, 115200) + the
 * semihosting console. WeAct F411CE.
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

using i_v = int (*)(void); using i_sdu = int (*)(const char*, const void*, unsigned);
using i_spuu = int (*)(const char*, void*, unsigned, unsigned*); using u_v = unsigned (*)(void);
using i_ucu = int (*)(unsigned, char*, unsigned*); using i_s = int (*)(const char*);

int main(void) {
    HAL_Init();
    uart_init();
    line("spifs-dynamic: POSIX dlopen of spifs.so (RAM / scl::dynamic-newlib)");

    line("  [1] dlopen...");
    void* h = dlopen(SCL_SO_DIR "/spifs.so", 0);
    if (!h) { line("  dlopen failed"); line("RESULT: FAILURE"); for (;;) {} }
    line("  [2] dlopen OK, dlsym...");

    auto spifs_init   = (i_v)   dlsym(h, "spifs_init");
    auto spifs_format = (i_v)   dlsym(h, "spifs_format");
    auto spifs_create = (i_sdu) dlsym(h, "spifs_create");
    auto spifs_count  = (u_v)   dlsym(h, "spifs_count");
    auto spifs_list   = (i_ucu) dlsym(h, "spifs_list");
    auto spifs_read   = (i_spuu)dlsym(h, "spifs_read");
    auto spifs_delete = (i_s)   dlsym(h, "spifs_delete");
    if (!spifs_init || !spifs_create || !spifs_read) { line("  dlsym failed"); line("RESULT: FAILURE"); for (;;) {} }
    line("  [3] dlsym OK, spifs_init...");

    if (spifs_init() != 0)  { line("  W25Q64 NOT found -- check SPI wiring"); for (;;) {} }
    line("  W25Q64 detected, SPI up");
    if (spifs_format() != 0){ line("  format FAILED"); for (;;) {} }
    line("  flatfs formatted");

    spifs_create("hello.txt", "Hello WeAct!\n", 13);
    spifs_create("nums.txt",  "1 2 3 4 5\n",    10);
    spifs_create("scl.txt",   "loaded by name\n", 15);
    char l[96]; std::snprintf(l, sizeof l, "  created 3 files; count=%u", spifs_count()); line(l);

    line("  -- ls --");
    for (unsigned i = 0; ; ++i) {
        char nm[32]; unsigned sz = 0;
        if (spifs_list(i, nm, &sz) != 0) break;
        std::snprintf(l, sizeof l, "    %s (%uB)", nm, sz); line(l);
    }
    line("  -- cat --");
    const char* files[] = { "hello.txt", "nums.txt", "scl.txt" };
    for (const char* fn : files) {
        char buf[64]; unsigned got = 0;
        if (spifs_read(fn, buf, sizeof buf - 1, &got) == 0) {
            if (got >= sizeof buf) got = sizeof buf - 1; buf[got] = 0;
            for (unsigned j = 0; j < got; ++j) if (buf[j] == '\n') buf[j] = 0;
            std::snprintf(l, sizeof l, "    %s: \"%s\"", fn, buf); line(l);
        }
    }
    spifs_delete("hello.txt");
    std::snprintf(l, sizeof l, "  deleted hello.txt; count=%u", spifs_count()); line(l);

    dlclose(h);
    line("RESULT: SUCCESS");
    for (;;) { }
}
