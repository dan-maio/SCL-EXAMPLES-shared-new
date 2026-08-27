/**
 * @file main.cpp — float-static: XIP + r10 demo (doc import_in_project_full §B).
 *
 * Loads floatlib.so (an r10-based PIC XIP library) via the STATIC loader: the
 * code executes IN PLACE from flash, only its RW segment is copied to RAM with
 * r10 = its GOT base. The exports are called BY NAME through the loader
 * (m.sym) + an r10-swap call. Results go to UART (USART1) and semihosting.
 */
#include "test-helpers.hpp"            // xload, r10_val, set_r10, rd, scl::Module
#include "diag/trace.h"
#include "firmware/semihost-file.hpp"
#include "fs-archive.hpp"
#include "scl/image-memory.hpp"
#include <cstdint>
#include <cstddef>
#include <span>
#include <cstdlib>
#include <cstdio>

extern "C" { void uart_init(void); void uart_puts(const char* s); }

#ifndef DL_LIB_DIR
#define DL_LIB_DIR "."
#endif

namespace {

void line(const char* s) { uart_puts(s); trace_printf("%s\n", s); }

char* utoa(unsigned v, char* p) {
    char t[10]; int i = 0;
    if (!v) { *p++ = '0'; return p; }
    while (v) { t[i++] = char('0' + v % 10); v /= 10; }
    while (i) *p++ = t[--i];
    return p;
}
void ftoa(float v, char* p) {
    if (v < 0) { *p++ = '-'; v = -v; }
    unsigned ip = (unsigned)v, fp = (unsigned)((v - (float)ip) * 1000.0f + 0.5f);
    p = utoa(ip, p); *p++ = '.';
    if (fp < 100) *p++ = '0'; if (fp < 10) *p++ = '0';
    p = utoa(fp, p); *p = 0;
}

// r10-aware FLOAT call (the u32 call_sym helpers can't carry float args/return):
// set r10 to the module's GOT base, then call through a typed float pointer.
float call2f(const scl::Module& m, const char* name, float a, float b) {
    void* fn = m.sym(name);
    if (!fn) { line("  [err] sym not found"); return 0; }
    r10_val = reinterpret_cast<void*>(m.get_smodule().got_base);
    return reinterpret_cast<float(*)(float,float)>(fn)(a, b);
}

// Read floatlib.so from the host over semihosting into the on-chip flat-fs.
bool provide(const char* name) {
    char path[160]; std::snprintf(path, sizeof(path), "%s/%s", DL_LIB_DIR, name);
    firmware::semihost_file src(path);
    if (!src.is_open()) { line("absent"); return false; }
    const std::size_t sz = src.size();
    auto memp = fw::image_arena().alloc(sz);
    auto* buf = static_cast<std::byte*>(*memp);
    bool ok = src.read_at(0, std::span<std::byte>(buf, sz)).has_value() &&
              fw::fs_add(name, buf, (std::uint32_t)sz).has_value();
    fw::image_arena().free(buf);
    return ok;
}

}  // namespace

extern "C" int main(void) {
    uart_init();
    line("float-static: loading floatlib.so over semihosting (XIP + r10)...");

    fw::fs_format();
    if (!provide("floatlib.so")) { line("float-static: PROVISION FAILED"); std::exit(1); }

    scl::Module m;
    if (!xload("floatlib.so", m)) { line("float-static: LOAD FAILED"); std::exit(1); }
    line(m.get_smodule().ok ? "float-static: loaded XIP (code from flash)"
                            : "float-static: loaded (copy-whole)");

    const float a = 2.5f, b = 3.5f;
    char l[96], sa[24], sb[24], sr[24]; int k;
    ftoa(a, sa); ftoa(b, sb);

    ftoa(call2f(m, "float_add", a, b), sr);   // 6.000
    k=0; for (const char* s="float_add(";*s;) l[k++]=*s++;
    for (char* s=sa;*s;) l[k++]=*s++; l[k++]=','; l[k++]=' ';
    for (char* s=sb;*s;) l[k++]=*s++; l[k++]=')'; l[k++]='='; l[k++]=' ';
    for (char* s=sr;*s;) l[k++]=*s++; l[k]=0; line(l);

    ftoa(call2f(m, "float_mul", a, b), sr);   // 8.750
    k=0; for (const char* s="float_mul(";*s;) l[k++]=*s++;
    for (char* s=sa;*s;) l[k++]=*s++; l[k++]=','; l[k++]=' ';
    for (char* s=sb;*s;) l[k++]=*s++; l[k++]=')'; l[k++]='='; l[k++]=' ';
    for (char* s=sr;*s;) l[k++]=*s++; l[k]=0; line(l);

    k=0; for (const char* s="floatlib calls = ";*s;) l[k++]=*s++;
    { char* e = utoa(rd(m, "calls"), l+k); *e = 0; } line(l);

    line("float-static: done");
    std::exit(0);
    for (;;) {}
}
