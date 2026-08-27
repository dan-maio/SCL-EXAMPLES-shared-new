/**
 * @file main.cpp — spifs-reloc: SPI-flatfs fixture over the RAM/ET_REL loader.
 *
 * Loads spifs.rel.o (W25Q64 HAL driver + flatfs, a loadable ET_REL module) over
 * SEMIHOSTING into a RAM arena, resolves its HAL/libc imports against this
 * firmware, then drives the on-board W25Q64 SPI flash BY NAME: init, format,
 * create/read/list/delete files. All output on USART1 (PA9, 115200) + semihosting.
 * WeAct-only (needs the physical SPI flash).
 */
#include "board.h"
#include "semihost.h"
#include "reloc/reloc-loader.hpp"
#include "scl/link-context.hpp"
#include "stm32f4xx_hal.h"      // HAL_Init (main starts the HAL tick)
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#ifndef RELOC_LIB_DIR
#define RELOC_LIB_DIR "."
#endif


// ---- spifs.rel.o exported ABI (its API), by hand --------------------------
using fn_i_v   = int (*)(void);
using fn_u_v   = unsigned (*)(void);
using fn_i_sdu = int (*)(const char*, const void*, unsigned);
using fn_i_s   = int (*)(const char*);
using fn_i_usu = int (*)(unsigned, char*, unsigned*);
using fn_i_spuu= int (*)(const char*, void*, unsigned, unsigned*);

namespace {

void line(const char* s) {
#if defined(SCL_BOARD_WEACT)
    board_uart_puts(s);
    board_uart_putc('\r'); board_uart_putc('\n');   // terminate the UART line
#endif
    trace_puts(s); trace_putc('\n');
}
char* utoa(unsigned v, char* p) { char t[10]; int i=0;
    if(!v){*p++='0';return p;} while(v){t[i++]=char('0'+v%10);v/=10;} while(i)*p++=t[--i]; return p; }

// Resolver: the module's imports (HAL + libc it borrows) are resolved against
// THIS firmware's own dynamic symbol table via link_context::from_self() — no
// table in code. Symbols are kept + exported by -Wl,-E and the -u anchors in
// CMakeLists (a build concern, not code). The firmware is linked -pie so it has
// a _DYNAMIC for from_self() to read.
loader::result<std::uint32_t> self_resolve(void* ctx, std::string_view n) {
    return static_cast<const fw::link_context*>(ctx)->resolve(n);
}

alignas(8) std::byte g_img[24*1024];
alignas(8) std::byte g_arena[16*1024];

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

reloc::module g_mod;
template <class F> F sym(const char* n){ return reinterpret_cast<F>((std::uintptr_t)reloc::sym(g_mod,n)); }

}  // namespace

extern "C" int main(void) {
    board_init();
#if defined(SCL_BOARD_WEACT)
    board_uart_init();
#endif
    HAL_Init();                                    // start the HAL tick for the SPI driver
    line("spifs-reloc: SPI flatfs fixture over RAM/ET_REL loader");

    auto self = fw::link_context::from_self();     // resolve module imports vs main's .dynsym
    if (!self) { line("  no _DYNAMIC (link -pie)"); semihost_exit(1); }
    static fw::link_context g_self = *self;

    std::size_t sz = read_mod("spifs.rel.o");
    if (!sz) { line("  read FAILED"); semihost_exit(1); }
    auto r = reloc::load(std::span<const std::byte>(g_img,sz), std::span(g_arena),
                         loader::symbol_source{self_resolve, &g_self});
    if (!r) { line("  LOAD FAILED"); semihost_exit(1); }
    g_mod = *r;
    line("  spifs.rel.o loaded + linked");

    auto sp_init   = sym<fn_i_v>("spifs_init");
    auto sp_format = sym<fn_i_v>("spifs_format");
    auto sp_create = sym<fn_i_sdu>("spifs_create");
    auto sp_delete = sym<fn_i_s>("spifs_delete");
    auto sp_count  = sym<fn_u_v>("spifs_count");
    auto sp_list   = sym<fn_i_usu>("spifs_list");
    auto sp_read   = sym<fn_i_spuu>("spifs_read");

    // ---- init the W25Q64 ----
    if (sp_init() != 0) { line("  W25Q64 NOT found — check SPI wiring"); semihost_exit(1); }
    line("  W25Q64 detected, SPI up");

    // ---- format ----
    if (sp_format() != 0) { line("  format FAILED"); semihost_exit(1); }
    line("  flatfs formatted");

    // ---- create files ----
    sp_create("hello.txt", "Hello WeAct!\n", 13);
    sp_create("nums.txt",  "1 2 3 4 5\n",    10);
    sp_create("scl.txt",   "loaded by name\n", 15);
    { char l[64]; int k=0; for(const char* s="  created 3 files; count=";*s;)l[k++]=*s++;
      char* e=utoa(sp_count(),l+k); *e=0; line(l); }

    // ---- list dir ----
    line("  -- ls --");
    for (unsigned i=0;; ++i) {
        char name[17]; unsigned fsz=0;
        if (sp_list(i,name,&fsz)!=0) break;
        char l[64]; int k=0; l[k++]=' ';l[k++]=' ';l[k++]=' ';l[k++]=' ';
        for(char* s=name;*s;)l[k++]=*s++; l[k++]=' ';l[k++]='(';
        char* e=utoa(fsz,l+k); k=e-l; l[k++]='B';l[k++]=')'; l[k]=0; line(l);
    }

    // ---- print contents ----
    line("  -- cat --");
    const char* files[3] = { "hello.txt", "nums.txt", "scl.txt" };
    for (int f=0; f<3; ++f) {
        char buf[128]; unsigned got=0;
        if (sp_read(files[f],buf,sizeof(buf)-1,&got)==0) {
            buf[got<sizeof(buf)?got:sizeof(buf)-1]=0;
            // trim trailing newline for a tidy one-liner
            for (unsigned j=0;j<got;++j) if(buf[j]=='\n') buf[j]=0;
            char l[160]; int k=0; l[k++]=' ';l[k++]=' ';l[k++]=' ';l[k++]=' ';
            for(const char* s=files[f];*s;)l[k++]=*s++; l[k++]=':';l[k++]=' ';l[k++]='"';
            for(char* s=buf;*s;)l[k++]=*s++; l[k++]='"'; l[k]=0; line(l);
        }
    }

    // ---- delete one, show it's gone ----
    sp_delete("hello.txt");
    { char l[64]; int k=0; for(const char* s="  deleted hello.txt; count=";*s;)l[k++]=*s++;
      char* e=utoa(sp_count(),l+k); *e=0; line(l); }
    line("  -- ls after delete --");
    for (unsigned i=0;; ++i) {
        char name[17]; unsigned fsz=0;
        if (sp_list(i,name,&fsz)!=0) break;
        char l[64]; int k=0; l[k++]=' ';l[k++]=' ';l[k++]=' ';l[k++]=' ';
        for(char* s=name;*s;)l[k++]=*s++; l[k]=0; line(l);
    }

    line("spifs-reloc: done");
    semihost_exit(0);
    for(;;){}
}
