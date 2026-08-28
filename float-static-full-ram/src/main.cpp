/**
 * @file main.cpp — float-static-full-ram: PURE static loader, module FULL in RAM.
 *
 * Loads floatlib.so (a plain -fPIC .so: hard-float add/mul) over SEMIHOSTING
 * WHOLE into a RAM buffer and runs it there via scl::static — no XIP, no r10
 * (PC-relative GOT). load_file() patches the module's GOT (imports resolved from
 * g_syms) AND main's GOT (patch_main), so the exports are ordinary DIRECT calls.
 * Minimal: register main's symbols, one load_file(), then call by name.
 * Output on USART1 + semihosting.
 */
#include "module.h"                  // scl::Module (the static loader)
#include "elf-reader.h"              // elf_loader::symbol_table
#include "diag/trace.h"
#include "stm32f4xx_hal.h"           // HAL_Init
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>

extern "C" { void uart_init(void); void uart_puts(const char* s); }

// the fixture's exports — declared by hand (its API), called DIRECTLY by name
extern "C" {
float float_add(float a, float b);
float float_mul(float a, float b);
}

#ifndef DL_LIB_DIR
#define DL_LIB_DIR "."
#endif

static elf_loader::symbol_table g_syms;      // main's exports the module resolves against
alignas(8) static uint8_t g_exec[16 * 1024]; // RAM the whole .so is loaded into

static void line(const char* s) { uart_puts(s); trace_printf("%s\n", s); }

// print "<label> = <int>.<3-decimals>" for a float (avoids pulling printf %f)
static void linef(const char* label, float v) {
    char l[80]; int k = 0;
    for (const char* s = label; *s; ) l[k++] = *s++;
    l[k++] = ' '; l[k++] = '='; l[k++] = ' ';
    unsigned whole = (unsigned)v;
    unsigned frac  = (unsigned)((v - (float)whole) * 1000.0f + 0.5f);
    char t[10]; int i = 0; unsigned w = whole;
    if (!w) t[i++] = '0'; while (w) { t[i++] = char('0' + w % 10); w /= 10; }
    while (i) l[k++] = t[--i];
    l[k++] = '.';
    l[k++] = char('0' + (frac / 100) % 10);
    l[k++] = char('0' + (frac / 10) % 10);
    l[k++] = char('0' + frac % 10);
    l[k] = 0;
    line(l);
}

extern "C" int main(void) {
    uart_init();
    HAL_Init();
    line("float-static-full-ram: float add/mul fixture, module FULL in RAM (scl::static)");

    // register the symbols the module imports from main (the loader's symbol table)
    #define REG(f) g_syms.add_global(#f, (uint32_t)(uintptr_t)(void*)(f))
    REG(memcpy); REG(memset);
    #undef REG

    scl::Module m;
    if (!m.load_file(DL_LIB_DIR "/floatlib.so", g_exec, sizeof g_exec, &g_syms)) {
        line("  LOAD FAILED"); std::exit(1);
    }
    line("  floatlib.so loaded WHOLE into RAM + linked (main GOT patched)");

    // ---- call the module's hard-float exports by DIRECT by-name calls ----
    float sum  = float_add(2.5f, 3.5f);   // expect 6.000
    float prod = float_mul(2.5f, 3.5f);   // expect 8.750
    linef("  float_add(2.5, 3.5)", sum);
    linef("  float_mul(2.5, 3.5)", prod);

    line("float-static-full-ram: done");
    std::exit(0);
    for(;;){}
}
