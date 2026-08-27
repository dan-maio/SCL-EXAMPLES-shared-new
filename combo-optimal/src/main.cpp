/**
 * @file main.cpp — combo-optimal: float module -> unload -> spi module (XIP no-r10).
 *
 * Sequential, semihosting-per-module:
 *   1. semihost floatlib.so into the flat-fs, load it into the auto-derived SRAM
 *      window, use it: sum/prod kept in main (bare direct calls, no r10);
 *   2. unload the float module (scoped optimal::Module dtor -> xip::nopic::unload);
 *   3. semihost spifs.so into the SAME window, init+format the W25Q64 flatfs,
 *      write the two kept values to a file, read it back;
 *   4. print to USART1 (PA9, 115200) + semihosting. WeAct-only.
 * Both .so are linked at the same window (loaded one at a time into it).
 */
#include "optimal/loader.hpp"
#include "firmware/semihost-file.hpp"
#include "fs-archive.hpp"
#include "scl/image-memory.hpp"
#include "diag/trace.h"
#include "stm32f4xx_hal.h"
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <span>

extern "C" { void uart_init(void); void uart_puts(const char* s); }

extern "C" {
float    float_add(float, float);
float    float_mul(float, float);
int      spifs_init(void);
int      spifs_format(void);
int      spifs_create(const char* name, const void* data, unsigned len);
int      spifs_read(const char* name, void* buf, unsigned cap, unsigned* out_len);
}

#ifndef DL_LIB_DIR
#define DL_LIB_DIR "."
#endif

namespace {
void line(const char* s) { uart_puts(s); trace_printf("%s\n", s); }
char* utoa(unsigned v, char* p){ char t[10]; int i=0;
    if(!v){*p++='0';return p;} while(v){t[i++]=char('0'+v%10);v/=10;} while(i)*p++=t[--i]; return p; }
char* ftoa(float v, char* p){ if(v<0){*p++='-';v=-v;}
    unsigned ip=(unsigned)v, fp=(unsigned)((v-(float)ip)*1000.0f+0.5f);
    p=utoa(ip,p); *p++='.'; if(fp<100)*p++='0'; if(fp<10)*p++='0'; return utoa(fp,p); }

// semihost a .so into the on-chip flat-fs; return its blob span.
std::span<const std::byte> provide(const char* name) {
    char path[160]; std::snprintf(path, sizeof path, "%s/%s", DL_LIB_DIR, name);
    firmware::semihost_file src(path);
    if (!src.is_open()) { line("  absent"); return {}; }
    const std::size_t sz = src.size();
    auto memp = fw::image_arena().alloc(sz);
    auto* buf = static_cast<std::byte*>(*memp);
    bool ok = src.read_at(0, std::span<std::byte>(buf, sz)).has_value() &&
              fw::fs_add(name, buf, (std::uint32_t)sz).has_value();
    fw::image_arena().free(buf);
    if (!ok) return {};
    const fw::FsEntry* e = fw::fs_find(name);
    return e ? std::span<const std::byte>(fw::fs_blob(*e), e->size) : std::span<const std::byte>{};
}
}  // namespace

extern "C" int main(void) {
    uart_init();
    HAL_Init();
    line("combo-optimal: float module -> unload -> spi module (XIP no-r10)");
    fw::fs_format();                              // the XIP flat-fs that HOLDS the .so modules

    const float a = 2.5f, b = 3.5f;
    float sum = 0, prod = 0;

    // ===== 1) semihost + load the FLOAT module, compute, keep values ==========
    {
        std::span<const std::byte> blob = provide("floatlib.so");
        optimal::Module mf[1];                    // scoped: dtor -> xip::nopic::unload
        if (optimal::load_and_link(&blob, 1, mf) != 1) { line("  float LOAD FAILED"); std::exit(1); }
        sum  = float_add(a, b);                   // bare direct calls, no r10
        prod = float_mul(a, b);
        char l[80],sa[16],sb[16]; *ftoa(a,sa)=0; *ftoa(b,sb)=0;
        int k=0; for(const char* s="  floatlib: sum(";*s;)l[k++]=*s++;
        for(char*s=sa;*s;)l[k++]=*s++; l[k++]=',';l[k++]=' '; for(char*s=sb;*s;)l[k++]=*s++;
        l[k++]=')'; l[k++]='='; { char* e=ftoa(sum,l+k); k=e-l; }
        for(const char* s="  prod=";*s;)l[k++]=*s++; { char* e=ftoa(prod,l+k); *e=0; } line(l);
    }                                             // <- 2) float module UNLOADED here
    line("  float module unloaded (window reused)");

    // ===== 3) semihost + load the SPI module into the SAME window =============
    {
        std::span<const std::byte> blob = provide("spifs.so");
        optimal::Module ms[1];
        if (optimal::load_and_link(&blob, 1, ms) != 1) { line("  spi LOAD FAILED"); std::exit(1); }
        if (spifs_init() != 0) { line("  W25Q64 NOT found -- check SPI wiring"); std::exit(1); }
        line("  spifs.so loaded; W25Q64 up");
        if (spifs_format() != 0) { line("  format FAILED"); std::exit(1); }

        char body[64]; int k=0;
        for(const char* s="sum=";*s;)body[k++]=*s++; { char* e=ftoa(sum,body+k); k=e-body; }
        for(const char* s=" prod=";*s;)body[k++]=*s++; { char* e=ftoa(prod,body+k); k=e-body; }
        body[k++]='\n';
        spifs_create("calc.txt", body, (unsigned)k);
        line("  wrote calc.txt to the SPI flatfs");

        char buf[64]; unsigned got=0;
        if (spifs_read("calc.txt", buf, sizeof buf-1, &got) == 0) {
            if (got>=sizeof buf) got=sizeof buf-1; buf[got]=0;
            for (unsigned j=0;j<got;++j) if(buf[j]=='\n') buf[j]=0;
            char l[96]; int m=0; for(const char* s="  read back calc.txt: \"";*s;)l[m++]=*s++;
            for(char*s=buf;*s;)l[m++]=*s++; l[m++]='"'; l[m]=0; line(l);
        }
    }

    line("combo-optimal: done");
    std::exit(0);
    for(;;){}
}
