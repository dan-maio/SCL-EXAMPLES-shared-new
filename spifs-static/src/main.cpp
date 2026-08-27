/**
 * @file main.cpp — spifs-static: SPI-flatfs fixture over the XIP+r10 static loader.
 *
 * Loads spifs.so (W25Q64 HAL driver + flatfs, an r10-based PIC XIP library) over
 * SEMIHOSTING into the on-chip flat-fs, loads it XIP (code from flash, RW in RAM,
 * r10 = GOT base), then drives the on-board W25Q64 BY NAME — DIRECT calls:
 * xload() patches main's GOT to the module's exports (patch_host_got), set_r10()
 * sets the PIC base once, and `spifs_init()` etc. are ordinary direct calls
 * (main is -fpic with no fixed pic-register, so leaving r10 at the module base is
 * safe). The module's HAL/libc imports resolve against main's .dynsym (-u anchors
 * keep them there). Output on USART1 + semihosting. WeAct-only (needs SPI flash).
 */
#include "test-helpers.hpp"            // xload, set_r10, scl::Module
#include "diag/trace.h"
#include "firmware/semihost-file.hpp"
#include "fs-archive.hpp"
#include "scl/image-memory.hpp"
#include "stm32f4xx_hal.h"            // HAL_Init
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <span>

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

namespace {

void line(const char* s) { uart_puts(s); trace_printf("%s\n", s); }   // uart.c appends CRLF
char* utoa(unsigned v, char* p){ char t[10]; int i=0;
    if(!v){*p++='0';return p;} while(v){t[i++]=char('0'+v%10);v/=10;} while(i)*p++=t[--i]; return p; }

// Read spifs.so from the host over semihosting into the on-chip flat-fs.
bool provide(const char* name) {
    char path[160]; std::snprintf(path, sizeof path, "%s/%s", DL_LIB_DIR, name);
    firmware::semihost_file src(path);
    if (!src.is_open()) { line("  absent"); return false; }
    const std::size_t sz = src.size();
    auto memp = fw::image_arena().alloc(sz);
    auto* buf = static_cast<std::byte*>(*memp);
    bool ok = src.read_at(0, std::span<std::byte>(buf, sz)).has_value();
    if (ok) {
        // Pre-bind the module's function imports (HAL/libc PLT slots) against
        // main's .dynsym BEFORE it is written to flash — XIP runs the .got.plt in
        // place, so those slots must already be resolved.
        xip::stat::bind_host_imports(std::span<std::byte>{buf, sz});
        ok = fw::fs_add(name, buf, (std::uint32_t)sz).has_value();
    }
    fw::image_arena().free(buf);
    return ok;
}

}  // namespace

extern "C" int main(void) {
    uart_init();
    HAL_Init();                       // HAL tick for the loaded SPI driver
    line("spifs-static: SPI flatfs fixture over XIP+r10 static loader");

    fw::fs_format();
    if (!provide("spifs.so")) { line("  PROVISION FAILED"); std::exit(1); }

    scl::Module m;
    if (!xload("spifs.so", m)) { line("  LOAD FAILED"); std::exit(1); }
    line(m.get_smodule().ok ? "  loaded XIP (code from flash)" : "  loaded (copy-whole)");
    set_r10(m);                       // r10 = module GOT base; main's GOT already patched -> direct calls

    // ---- init the W25Q64 (DIRECT by-name calls) ----
    if (spifs_init() != 0) { line("  W25Q64 NOT found — check SPI wiring"); std::exit(1); }
    line("  W25Q64 detected, SPI up");
    if (spifs_format() != 0) { line("  format FAILED"); std::exit(1); }
    line("  flatfs formatted");

    // ---- create files ----
    spifs_create("hello.txt", "Hello WeAct!\n", 13);
    spifs_create("nums.txt",  "1 2 3 4 5\n",    10);
    spifs_create("scl.txt",   "loaded by name\n", 15);
    { char l[64]; int k=0; for(const char* s="  created 3 files; count=";*s;)l[k++]=*s++;
      char* e=utoa(spifs_count(),l+k); *e=0; line(l); }

    // ---- list dir ----
    line("  -- ls --");
    for (unsigned i=0;; ++i) {
        char name[17]; unsigned fsz=0;
        if (spifs_list(i, name, &fsz) != 0) break;
        char l[64]; int k=0; l[k++]=' ';l[k++]=' ';l[k++]=' ';l[k++]=' ';
        for(char* s=name;*s;)l[k++]=*s++; l[k++]=' ';l[k++]='(';
        char* e=utoa(fsz,l+k); k=e-l; l[k++]='B';l[k++]=')'; l[k]=0; line(l);
    }

    // ---- print contents ----
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

    // ---- delete one, show it's gone ----
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

    line("spifs-static: done");
    std::exit(0);
    for(;;){}
}
