/**
 * @file main.cpp — combo-static-full-ram: float -> unload -> spi, module FULL in RAM.
 *
 * Pure static loader (scl::static), sequential + semihosting-per-module:
 *   1. semihost floatlib.so WHOLE into a RAM buffer, run it there, use it:
 *      sum/prod kept in main (bare direct calls; patch_main filled main's GOT);
 *   2. unload the float module (scl::Module dtor: byte-exact restore of main);
 *   3. semihost spifs.so into the SAME buffer, init+format the W25Q64 flatfs,
 *      write the two kept values to a file, read it back;
 *   4. print to USART1 (PA9, 115200) + semihosting. WeAct-only.
 */
#include "module.h"                   // scl::Module (the static loader)
#include "elf-reader.h"               // elf_loader::symbol_table
#include "diag/trace.h"
#include "stm32f4xx_hal.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>

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

static elf_loader::symbol_table g_syms;      // main's exports the modules resolve against
alignas(8) static uint8_t g_exec[48 * 1024]; // ONE RAM buffer, reused per module

static void line(const char* s) { uart_puts(s); trace_printf("%s\n", s); }
static char* utoa(unsigned v, char* p){ char t[10]; int i=0;
    if(!v){*p++='0';return p;} while(v){t[i++]=char('0'+v%10);v/=10;} while(i)*p++=t[--i]; return p; }
static char* ftoa(float v, char* p){ if(v<0){*p++='-';v=-v;}
    unsigned ip=(unsigned)v, fp=(unsigned)((v-(float)ip)*1000.0f+0.5f);
    p=utoa(ip,p); *p++='.'; if(fp<100)*p++='0'; if(fp<10)*p++='0'; return utoa(fp,p); }

extern "C" int main(void) {
    uart_init();
    HAL_Init();
    line("combo-static-full-ram: float -> unload -> spi, module FULL in RAM (scl::static)");

    #define REG(f) g_syms.add_global(#f, (uint32_t)(uintptr_t)(void*)(f))
    REG(HAL_GPIO_Init); REG(HAL_GPIO_WritePin);
    REG(HAL_SPI_Init);  REG(HAL_SPI_TransmitReceive); REG(HAL_SPI_Receive);
    REG(memcpy);        REG(memset);
    #undef REG

    const float a = 2.5f, b = 3.5f;
    float sum = 0, prod = 0;

    // ===== 1) semihost + load the FLOAT module WHOLE into RAM, compute, keep ===
    {
        scl::Module mf;                          // scoped: dtor unloads byte-exact
        if (!mf.load_file(DL_LIB_DIR "/floatlib.so", g_exec, sizeof g_exec, &g_syms)) {
            line("  float LOAD FAILED"); std::exit(1);
        }
        sum  = float_add(a, b);                  // bare direct calls
        prod = float_mul(a, b);
        char l[80],sa[16],sb[16]; *ftoa(a,sa)=0; *ftoa(b,sb)=0;
        int k=0; for(const char* s="  floatlib: sum(";*s;)l[k++]=*s++;
        for(char*s=sa;*s;)l[k++]=*s++; l[k++]=',';l[k++]=' '; for(char*s=sb;*s;)l[k++]=*s++;
        l[k++]=')'; l[k++]='='; { char* e=ftoa(sum,l+k); k=e-l; }
        for(const char* s="  prod=";*s;)l[k++]=*s++; { char* e=ftoa(prod,l+k); *e=0; } line(l);
    }                                            // <- 2) float module UNLOADED (byte-exact)
    line("  float module unloaded (main restored byte-exact)");

    // ===== 3) semihost + load the SPI module into the SAME buffer =============
    {
        scl::Module ms;
        if (!ms.load_file(DL_LIB_DIR "/spifs.so", g_exec, sizeof g_exec, &g_syms)) {
            line("  spi LOAD FAILED"); std::exit(1);
        }
        if (spifs_init() != 0) { line("  W25Q64 NOT found -- check SPI wiring"); std::exit(1); }
        line("  spifs.so loaded WHOLE into RAM; W25Q64 up");
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

    line("combo-static-full-ram: done");
    std::exit(0);
    for(;;){}
}
