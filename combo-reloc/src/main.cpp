/**
 * @file main.cpp — combo-reloc: float module → unload → spi module (RAM/ET_REL).
 *
 * Sequential, semihosting-per-module (not all upfront):
 *   1. semihost-read floatlib.rel.o into the arena, use it: sum = float_add(a,b),
 *      prod = float_mul(a,b) — kept in main as plain floats;
 *   2. "unload" the float module (reloc: just reuse the arena — no main state);
 *   3. semihost-read spifs.rel.o into the SAME arena, init+format the on-board
 *      W25Q64 flatfs, write the two kept values into a file, read it back;
 *   4. print to USART1 (PA9, 115200) + the semihosting console. WeAct-only.
 */
#include "board.h"
#include "semihost.h"
#include "reloc/reloc-loader.hpp"
#include "scl/link-context.hpp"
#include "stm32f4xx_hal.h"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#ifndef RELOC_LIB_DIR
#define RELOC_LIB_DIR "."
#endif

namespace {

void line(const char* s) {
#if defined(SCL_BOARD_WEACT)
    board_uart_puts(s); board_uart_putc('\r'); board_uart_putc('\n');
#endif
    trace_puts(s); trace_putc('\n');
}
char* utoa(unsigned v, char* p){ char t[10]; int i=0;
    if(!v){*p++='0';return p;} while(v){t[i++]=char('0'+v%10);v/=10;} while(i)*p++=t[--i]; return p; }
// float -> "int.frac" (3 decimals), no libc
char* ftoa(float v, char* p){
    if(v<0){*p++='-';v=-v;} unsigned ip=(unsigned)v, fp=(unsigned)((v-(float)ip)*1000.0f+0.5f);
    p=utoa(ip,p); *p++='.'; if(fp<100)*p++='0'; if(fp<10)*p++='0'; return utoa(fp,p); }

// Resolver: module imports (HAL/libc) resolve against main's .dynsym (from_self).
loader::result<std::uint32_t> self_resolve(void* ctx, std::string_view n) {
    return static_cast<const fw::link_context*>(ctx)->resolve(n);
}

alignas(8) std::byte g_img[24*1024];        // ONE image buffer, reused per module
alignas(8) std::byte g_arena[16*1024];      // ONE arena, reused per module
reloc::module g_mod;
template <class F> F sym(const char* n){ return reinterpret_cast<F>((std::uintptr_t)reloc::sym(g_mod,n)); }

// read RELOC_LIB_DIR/<name> over semihosting into g_img; return size.
std::size_t read_mod(const char* name) {
    char path[192]; int k=0;
    for (const char* p=RELOC_LIB_DIR "/"; *p; ++p) path[k++]=*p;
    for (const char* p=name; *p; ++p)               path[k++]=*p;
    path[k]=0;
    int h=semihost_open(path,1); if(h<0) return 0;
    long n=semihost_flen(h); std::size_t sz=0;
    if (n>0 && (std::size_t)n<=sizeof(g_img) && semihost_read(h,g_img,(int)n)==0) sz=(std::size_t)n;
    semihost_close(h); return sz;
}

using f_ff = float (*)(float,float);
using i_v  = int (*)(void);
using i_sdu= int (*)(const char*, const void*, unsigned);
using i_spuu=int (*)(const char*, void*, unsigned, unsigned*);

}  // namespace

extern "C" int main(void) {
    board_init();
#if defined(SCL_BOARD_WEACT)
    board_uart_init();
#endif
    HAL_Init();
    line("combo-reloc: float module -> unload -> spi module (RAM/ET_REL)");

    auto self = fw::link_context::from_self();
    if (!self) { line("  no _DYNAMIC (link -pie)"); semihost_exit(1); }
    static fw::link_context g_self = *self;
    loader::symbol_source rs{ self_resolve, &g_self };

    // ===== 1) semihost + load the FLOAT module, compute, keep values ==========
    const float a = 2.5f, b = 3.5f;
    float sum = 0, prod = 0;
    {
        std::size_t sz = read_mod("floatlib.rel.o");     // <- semihost the first module now
        if (!sz) { line("  float read FAILED"); semihost_exit(1); }
        auto r = reloc::load(std::span<const std::byte>(g_img,sz), std::span(g_arena), rs);
        if (!r) { line("  float LOAD FAILED"); semihost_exit(1); }
        g_mod = *r;
        auto fadd = sym<f_ff>("float_add");
        auto fmul = sym<f_ff>("float_mul");
        sum  = fadd(a, b);                                // 6.0
        prod = fmul(a, b);                                // 8.75
        char l[80],sa[16],sb[16]; *ftoa(a,sa)=0; *ftoa(b,sb)=0;
        int k=0; for(const char* s="  floatlib: sum(";*s;)l[k++]=*s++;
        for(char*s=sa;*s;)l[k++]=*s++; l[k++]=',';l[k++]=' '; for(char*s=sb;*s;)l[k++]=*s++;
        l[k++]=')'; l[k++]='='; { char* e=ftoa(sum,l+k); k=e-l; }
        for(const char* s="  prod=";*s;)l[k++]=*s++; { char* e=ftoa(prod,l+k); *e=0; } line(l);
    }
    // 2) UNLOAD the float module: reloc has no main state — just reuse the arena.
    line("  float module unloaded (arena reused)");

    // ===== 3) semihost + load the SPI module, write the kept values to a file ==
    {
        std::size_t sz = read_mod("spifs.rel.o");        // <- semihost the second module now
        if (!sz) { line("  spi read FAILED"); semihost_exit(1); }
        auto r = reloc::load(std::span<const std::byte>(g_img,sz), std::span(g_arena), rs);
        if (!r) { line("  spi LOAD FAILED"); semihost_exit(1); }
        g_mod = *r;

        auto sp_init  = sym<i_v>("spifs_init");
        auto sp_fmt   = sym<i_v>("spifs_format");
        auto sp_create= sym<i_sdu>("spifs_create");
        auto sp_read  = sym<i_spuu>("spifs_read");
        if (sp_init() != 0) { line("  W25Q64 NOT found — check SPI wiring"); semihost_exit(1); }
        line("  spifs.rel.o loaded; W25Q64 up");
        if (sp_fmt() != 0)  { line("  format FAILED"); semihost_exit(1); }

        // build "sum=6.000 prod=8.750\n" from the values kept across the swap
        char body[64]; int k=0;
        for(const char* s="sum=";*s;)body[k++]=*s++; { char* e=ftoa(sum,body+k); k=e-body; }
        for(const char* s=" prod=";*s;)body[k++]=*s++; { char* e=ftoa(prod,body+k); k=e-body; }
        body[k++]='\n';
        sp_create("calc.txt", body, (unsigned)k);
        line("  wrote calc.txt to the SPI flatfs");

        char buf[64]; unsigned got=0;
        if (sp_read("calc.txt", buf, sizeof buf-1, &got) == 0) {
            if (got>=sizeof buf) got=sizeof buf-1; buf[got]=0;
            for (unsigned j=0;j<got;++j) if(buf[j]=='\n') buf[j]=0;
            char l[96]; int m=0; for(const char* s="  read back calc.txt: \"";*s;)l[m++]=*s++;
            for(char*s=buf;*s;)l[m++]=*s++; l[m++]='"'; l[m]=0; line(l);
        }
    }

    line("combo-reloc: done");
    semihost_exit(0);
    for(;;){}
}
