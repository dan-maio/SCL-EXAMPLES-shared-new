/**
 * @file main.cpp — combo-static: float module -> unload -> spi module (XIP + r10).
 *
 * Sequential, semihosting-per-module:
 *   1. semihost floatlib.so into the flat-fs, load it XIP, use it: sum/prod kept
 *      in main as plain floats (bare direct calls, set_r10 once);
 *   2. unload the float module (scoped scl::Module dtor -> xip::stat::unload
 *      restores main's GOT);
 *   3. semihost spifs.so, load it XIP, init+format the on-board W25Q64 flatfs,
 *      write the two kept values to a file, read it back;
 *   4. print to USART1 (PA9, 115200) + semihosting. WeAct-only.
 */
#include "test-helpers.hpp"            // xload, set_r10, scl::Module
#include "diag/trace.h"
#include "firmware/semihost-file.hpp"
#include "fs-archive.hpp"
#include "scl/image-memory.hpp"
#include "stm32f4xx_hal.h"
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <span>

extern "C" { void uart_init(void); void uart_puts(const char* s); }

// exports we call directly by name (float module + spi module)
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

// semihost a .so into the on-chip flat-fs; pre-bind its function imports first.
bool provide(const char* name) {
    char path[160]; std::snprintf(path, sizeof path, "%s/%s", DL_LIB_DIR, name);
    firmware::semihost_file src(path);
    if (!src.is_open()) { line("  absent"); return false; }
    const std::size_t sz = src.size();
    auto memp = fw::image_arena().alloc(sz);
    auto* buf = static_cast<std::byte*>(*memp);
    bool ok = src.read_at(0, std::span<std::byte>(buf, sz)).has_value();
    if (ok) {
        xip::stat::bind_host_imports(std::span<std::byte>{buf, sz});   // HAL PLT slots (no-op for floatlib)
        ok = fw::fs_add(name, buf, (std::uint32_t)sz).has_value();
    }
    fw::image_arena().free(buf);
    return ok;
}
}  // namespace

extern "C" int main(void) {
    uart_init();
    HAL_Init();
    line("combo-static: float module -> unload -> spi module (XIP + r10)");
    fw::fs_format();                              // the XIP flat-fs that HOLDS the .so modules

    const float a = 2.5f, b = 3.5f;
    float sum = 0, prod = 0;

    // ===== 1) semihost + load the FLOAT module, compute, keep values ==========
    if (!provide("floatlib.so")) { line("  float provision FAILED"); std::exit(1); }
    {
        scl::Module mf;                           // scoped: dtor unloads (restores main's GOT)
        if (!xload("floatlib.so", mf)) { line("  float LOAD FAILED"); std::exit(1); }
        set_r10(mf);                              // r10 = float module GOT base
        sum  = float_add(a, b);                   // 6.0   -- bare direct calls
        prod = float_mul(a, b);                   // 8.75
        char l[80],sa[16],sb[16]; *ftoa(a,sa)=0; *ftoa(b,sb)=0;
        int k=0; for(const char* s="  floatlib: sum(";*s;)l[k++]=*s++;
        for(char*s=sa;*s;)l[k++]=*s++; l[k++]=',';l[k++]=' '; for(char*s=sb;*s;)l[k++]=*s++;
        l[k++]=')'; l[k++]='='; { char* e=ftoa(sum,l+k); k=e-l; }
        for(const char* s="  prod=";*s;)l[k++]=*s++; { char* e=ftoa(prod,l+k); *e=0; } line(l);
    }                                             // <- 2) float module UNLOADED here
    line("  float module unloaded (main GOT restored)");

    // ===== 3) semihost + load the SPI module, write the kept values ===========
    if (!provide("spifs.so")) { line("  spi provision FAILED"); std::exit(1); }
    {
        scl::Module ms;
        if (!xload("spifs.so", ms)) { line("  spi LOAD FAILED"); std::exit(1); }
        set_r10(ms);                              // r10 = spi module GOT base
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

    line("combo-static: done");
    std::exit(0);
    for(;;){}
}
