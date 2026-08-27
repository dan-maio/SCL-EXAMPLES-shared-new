/**
 * @file main.cpp — spi-static-full-ram: PURE static loader, module FULL in RAM.
 *
 * Loads spifs.so (a plain -fPIC .so: W25Q64 HAL driver + flatfs) over SEMIHOSTING
 * WHOLE into a RAM buffer and runs it there via scl::static — no XIP, no r10
 * (PC-relative GOT). load_file() patches the module's GOT (imports resolved from
 * g_syms) AND main's GOT (patch_main), so the exports are ordinary DIRECT calls.
 * Minimal: register main's symbols, one load_file(), then call by name.
 * Output on USART1 + semihosting. WeAct-only (drives the physical SPI flash).
 */
#include "module.h"                  // scl::Module (the static loader)
#include "elf-reader.h"              // elf_loader::symbol_table
#include "diag/trace.h"
#include "stm32f4xx_hal.h"           // HAL_Init + the HAL the module imports
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>

extern "C" { void uart_init(void); void uart_puts(const char* s); }

// the fixture's exports — declared by hand (its API), called DIRECTLY by name
extern "C" {
int      spifs_init(void);
int      spifs_format(void);
int      spifs_create(const char* name, const void* data, unsigned len);
int      spifs_delete(const char* name);
unsigned spifs_count(void);
int      spifs_list(unsigned idx, char* name_out, unsigned* size_out);
int      spifs_read(const char* name, void* buf, unsigned cap, unsigned* out_len);
}

#ifndef DL_LIB_DIR
#define DL_LIB_DIR "."
#endif

static elf_loader::symbol_table g_syms;      // main's exports the module resolves against
alignas(8) static uint8_t g_exec[48 * 1024]; // RAM the whole .so is loaded into

static void line(const char* s) { uart_puts(s); trace_printf("%s\n", s); }
static char* utoa(unsigned v, char* p){ char t[10]; int i=0;
    if(!v){*p++='0';return p;} while(v){t[i++]=char('0'+v%10);v/=10;} while(i)*p++=t[--i]; return p; }

extern "C" int main(void) {
    uart_init();
    HAL_Init();
    line("spi-static-full-ram: SPI flatfs fixture, module FULL in RAM (scl::static)");

    // register the symbols the module imports from main (the loader's symbol table)
    #define REG(f) g_syms.add_global(#f, (uint32_t)(uintptr_t)(void*)(f))
    REG(HAL_GPIO_Init); REG(HAL_GPIO_WritePin);
    REG(HAL_SPI_Init);  REG(HAL_SPI_TransmitReceive); REG(HAL_SPI_Receive);
    REG(memcpy);        REG(memset);
    #undef REG

    scl::Module m;
    if (!m.load_file(DL_LIB_DIR "/spifs.so", g_exec, sizeof g_exec, &g_syms)) {
        line("  LOAD FAILED"); std::exit(1);
    }
    line("  spifs.so loaded WHOLE into RAM + linked (main GOT patched)");

    // ---- drive the on-board W25Q64 by DIRECT by-name calls ----
    if (spifs_init() != 0) { line("  W25Q64 NOT found — check SPI wiring"); std::exit(1); }
    line("  W25Q64 detected, SPI up");
    if (spifs_format() != 0) { line("  format FAILED"); std::exit(1); }
    line("  flatfs formatted");

    spifs_create("hello.txt", "Hello WeAct!\n", 13);
    spifs_create("nums.txt",  "1 2 3 4 5\n",    10);
    spifs_create("scl.txt",   "loaded by name\n", 15);
    { char l[64]; int k=0; for(const char* s="  created 3 files; count=";*s;)l[k++]=*s++;
      char* e=utoa(spifs_count(),l+k); *e=0; line(l); }

    line("  -- ls --");
    for (unsigned i=0;; ++i) {
        char name[17]; unsigned fsz=0;
        if (spifs_list(i, name, &fsz) != 0) break;
        char l[64]; int k=0; l[k++]=' ';l[k++]=' ';l[k++]=' ';l[k++]=' ';
        for(char* s=name;*s;)l[k++]=*s++; l[k++]=' ';l[k++]='(';
        char* e=utoa(fsz,l+k); k=e-l; l[k++]='B';l[k++]=')'; l[k]=0; line(l);
    }

    line("  -- cat --");
    const char* files[3] = { "hello.txt", "nums.txt", "scl.txt" };
    for (int f=0; f<3; ++f) {
        char buf[128]; unsigned got=0;
        if (spifs_read(files[f], buf, sizeof buf - 1, &got) == 0) {
            if (got>=sizeof buf) got=sizeof buf-1; buf[got]=0;
            for (unsigned j=0;j<got;++j) if(buf[j]=='\n') buf[j]=0;
            char l[160]; int k=0; l[k++]=' ';l[k++]=' ';l[k++]=' ';l[k++]=' ';
            for(const char* s=files[f];*s;)l[k++]=*s++; l[k++]=':';l[k++]=' ';l[k++]='"';
            for(char* s=buf;*s;)l[k++]=*s++; l[k++]='"'; l[k]=0; line(l);
        }
    }

    spifs_delete("hello.txt");
    { char l[64]; int k=0; for(const char* s="  deleted hello.txt; count=";*s;)l[k++]=*s++;
      char* e=utoa(spifs_count(),l+k); *e=0; line(l); }
    line("  -- ls after delete --");
    for (unsigned i=0;; ++i) {
        char name[17]; unsigned fsz=0;
        if (spifs_list(i, name, &fsz) != 0) break;
        char l[64]; int k=0; l[k++]=' ';l[k++]=' ';l[k++]=' ';l[k++]=' ';
        for(char* s=name;*s;)l[k++]=*s++; l[k]=0; line(l);
    }

    line("spi-static-full-ram: done");
    std::exit(0);
    for(;;){}
}
