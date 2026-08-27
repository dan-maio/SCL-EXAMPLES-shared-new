/**
 * @file main.cpp — float-reloc: RAM-execution demo (doc import_in_project_full §A).
 *
 * Loads floatlib.rel.o (an ET_REL module) over semihosting into a plain RAM
 * arena with scl::reloc, then calls its hard-float add/multiply BY resolved
 * pointer (reloc::sym), printing results to the WeAct UART (USART1) AND the
 * semihosting console. Firmware is an ordinary NON-PIC executable — no -pie, no
 * windows, no r10.
 */
#include "board.h"
#include "semihost.h"
#include "reloc/reloc-loader.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#ifndef RELOC_LIB_DIR
#define RELOC_LIB_DIR "."
#endif

// ---- floatlib's exports (its API), declared by hand -------------------------
extern "C" {
float    float_add(float, float);
float    float_mul(float, float);
extern unsigned calls;
}

namespace {

// Print one line to BOTH the UART and the semihosting console.
void line(const char* s) {
#if defined(SCL_BOARD_WEACT)
    board_uart_puts(s);        // WeAct: also to USART1 (PA9, 115200)
#endif
    trace_puts(s); trace_putc('\n');   // semihosting console (QEMU + board)
}

// tiny unsigned -> decimal
char* utoa(unsigned v, char* p) {
    char t[10]; int i = 0;
    if (!v) { *p++ = '0'; return p; }
    while (v) { t[i++] = char('0' + v % 10); v /= 10; }
    while (i) *p++ = t[--i];
    return p;
}
// float -> "int.frac" (3 decimals), no libc
void ftoa(float v, char* p) {
    if (v < 0) { *p++ = '-'; v = -v; }
    unsigned ip = (unsigned)v;
    unsigned fp = (unsigned)((v - (float)ip) * 1000.0f + 0.5f);
    p = utoa(ip, p); *p++ = '.';
    if (fp < 100) *p++ = '0'; if (fp < 10) *p++ = '0';
    p = utoa(fp, p); *p = 0;
}

// The library has no imports; a resolver is still required by the API.
loader::result<std::uint32_t> resolve(void*, std::string_view) {
    return loader::unexpected(loader::error::SYMBOL_NOT_FOUND);
}

alignas(8) std::byte g_img[8 * 1024];
alignas(8) std::byte g_arena[8 * 1024];       // plain .bss — NOT a reserved window

std::size_t read_mod(const char* name) {
    char path[160]; int k = 0;
    for (const char* p = RELOC_LIB_DIR "/"; *p; ++p) path[k++] = *p;
    for (const char* p = name; *p; ++p)         path[k++] = *p;
    path[k] = 0;
    int h = semihost_open(path, 1);
    if (h < 0) return 0;
    long n = semihost_flen(h);
    std::size_t sz = 0;
    if (n > 0 && (std::size_t)n <= sizeof(g_img) && semihost_read(h, g_img, (int)n) == 0)
        sz = (std::size_t)n;
    semihost_close(h);
    return sz;
}

}  // namespace

extern "C" int main(void) {
    board_init();
#if defined(SCL_BOARD_WEACT)
    board_uart_init();
#endif
    line("float-reloc: loading floatlib.rel.o over semihosting (RAM/ET_REL)...");

    std::size_t sz = read_mod("floatlib.rel.o");
    if (!sz) { line("float-reloc: read FAILED"); semihost_exit(1); }

    auto r = reloc::load(std::span<const std::byte>(g_img, sz), std::span(g_arena),
                         loader::symbol_source{ resolve, nullptr });
    if (!r) { line("float-reloc: LOAD FAILED"); semihost_exit(1); }

    // resolve the exports to plain function pointers and call them
    auto p_add = reinterpret_cast<float(*)(float,float)>((std::uintptr_t)reloc::sym(*r, "float_add"));
    auto p_mul = reinterpret_cast<float(*)(float,float)>((std::uintptr_t)reloc::sym(*r, "float_mul"));
    auto p_cal = reinterpret_cast<unsigned*>((std::uintptr_t)reloc::sym(*r, "calls"));

    const float a = 2.5f, b = 3.5f;
    char l[96], sa[24], sb[24], sr[24]; int k;
    ftoa(a, sa); ftoa(b, sb);

    ftoa(p_add(a, b), sr);   // 6.000
    k = 0; for (const char* s="float_add(";*s;) l[k++]=*s++;
    for (char* s=sa;*s;) l[k++]=*s++; l[k++]=','; l[k++]=' ';
    for (char* s=sb;*s;) l[k++]=*s++; l[k++]=')'; l[k++]='='; l[k++]=' ';
    for (char* s=sr;*s;) l[k++]=*s++; l[k]=0; line(l);

    ftoa(p_mul(a, b), sr);   // 8.750
    k = 0; for (const char* s="float_mul(";*s;) l[k++]=*s++;
    for (char* s=sa;*s;) l[k++]=*s++; l[k++]=','; l[k++]=' ';
    for (char* s=sb;*s;) l[k++]=*s++; l[k++]=')'; l[k++]='='; l[k++]=' ';
    for (char* s=sr;*s;) l[k++]=*s++; l[k]=0; line(l);

    k = 0; for (const char* s="floatlib calls = ";*s;) l[k++]=*s++;
    { char* e = utoa(p_cal ? *p_cal : 0u, l+k); *e = 0; } line(l);

    line("float-reloc: done");
    semihost_exit(0);
    for (;;) {}
}
