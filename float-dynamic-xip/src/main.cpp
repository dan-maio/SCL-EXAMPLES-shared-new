/**
 * @file main.cpp — float-dynamic-xip: load floatlib.so with the POSIX dlopen API,
 *        executing the module's code IN PLACE from flash (XIP + r10).
 *
 * Model F/XIP (import_in_project_full.md): scl::xip-dynamic-newlib. The module
 * is an r10-based split .so; its R-X segment runs from flash (only the RW/GOT
 * segment is copied to RAM). We keep the standard POSIX surface —
 * dlopen/dlsym/dlclose — where dlopen provisions the .so into the on-chip flat-fs
 * and XIP-loads it, and imports resolve via the link-time embedded symtab. The
 * only XIP-specific twist is the call: r10 must point at the module's GOT base,
 * so set it once per module (call_f), then call the export as a normal pointer.
 * Output on USART1 (PA9, 115200) + the semihosting console. WeAct F411CE.
 */
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <span>
#include "uart.h"
#include "firmware/semihost-file.hpp"
#include "scl/image-memory.hpp"
#include "fs-archive.hpp"
#include "xip/segment-map.hpp"
#include "xip/relocate.hpp"
#include "xip/dynamic-newlib/adapter.hpp"

#ifndef SCL_SO_DIR
#define SCL_SO_DIR "."
#endif

/* the embedded symbol table (baked into flash by gen-symtab) */
extern "C" const std::uint8_t scl_symtab_blob[];
constexpr std::uint32_t kSymtabMagic = 0x534C4353;   /* 'SCLS' */

/* r10 = the module GOT base register; set it before an XIP call */
register void* r10_val asm("r10");

static void line(const char* s) { uart_puts(s); puts(s); }
static void fstr(float v, char* p) {
    if (v < 0) { *p++ = '-'; v = -v; }
    unsigned ip = (unsigned)v, fp = (unsigned)((v - (float)ip) * 1000.0f + 0.5f);
    char* q = p; int n = 0; unsigned t = ip; do { ++n; t /= 10; } while (t);
    q += n; *q-- = 0; t = ip; do { *q-- = char('0' + t % 10); t /= 10; } while (t);
    p += n; *p++ = '.'; *p++ = char('0' + fp/100%10); *p++ = char('0' + fp/10%10);
    *p++ = char('0' + fp%10); *p = 0;
}

/* --- the POSIX dlopen API, over the XIP dynamic loader ---------------------- */
static bool load_symtab() {
    std::uint32_t magic, n;
    std::memcpy(&magic, scl_symtab_blob, 4);
    std::memcpy(&n, scl_symtab_blob + 4, 4);
    if (magic != kSymtabMagic || n == 0) { line("[symtab] missing"); return false; }
    xip::newlib::set_symtab(reinterpret_cast<const firmware::sym_entry*>(scl_symtab_blob + 8), n);
    return true;
}
static bool can_xip(std::span<const std::byte> img) noexcept {
    xip::SegmentMap seg;
    return xip::classify_segments(img, seg) && seg.split && xip::text_is_immutable(img, seg);
}
/* dlopen(path): semihost-read the .so into the flat-fs, XIP-load it, return handle */
static void* dlopen(const char* path, int) {
    const char* name = std::strrchr(path, '/'); name = name ? name + 1 : path;
    firmware::semihost_file src(path);
    if (!src.is_open()) return nullptr;
    const std::size_t sz = src.size();
    auto memp = fw::image_arena().alloc(sz);
    if (!memp) return nullptr;
    auto* buf = static_cast<std::byte*>(*memp);
    void* h = nullptr;
    if (src.read_at(0, std::span<std::byte>{buf, sz}).has_value()) {
        if (can_xip(std::span<const std::byte>{buf, sz}))
            xip::newlib::patch_main_imports(std::span<std::byte>{buf, sz});   /* pre-bind PLT for XIP */
        if (fw::fs_add(name, buf, (std::uint32_t)sz).has_value()) {
            const fw::FsEntry* e = fw::fs_find(name);
            if (e) h = xip::newlib::dlopen(std::span<const std::byte>{ fw::fs_blob(*e), e->size }, name);
        }
    }
    fw::image_arena().free(buf);
    return h;
}
static void* dlsym(void* h, const char* n) { return xip::newlib::dlsym(h, n); }
static void  dlclose(void* h)              { xip::newlib::dlclose(h); }

/* an r10-aware float call: set r10 = the module's GOT base, then call by pointer */
static float call_f(void* h, const char* name, float a, float b) {
    void* fn = dlsym(h, name);
    if (!fn) return 0;
    r10_val = reinterpret_cast<void*>(xip::newlib::get_got_base(h));
    return reinterpret_cast<float(*)(float,float)>(fn)(a, b);
}

int main(void) {
    uart_init();
    line("float-dynamic-xip: POSIX dlopen of floatlib.so (XIP+r10 / scl::xip-dynamic-newlib)");
    if (!load_symtab()) { line("RESULT: FAILURE"); for (;;) {} }
    fw::fs_format();

    void* h = dlopen(SCL_SO_DIR "/floatlib.so", 0);
    if (!h) { line("  dlopen failed"); line("RESULT: FAILURE"); for (;;) {} }
    line("  loaded XIP (code from flash)");

    char sa[16], sm[16], l[80];
    fstr(call_f(h, "float_add", 2.5f, 3.5f), sa);   /* 6.000 */
    fstr(call_f(h, "float_mul", 2.5f, 3.5f), sm);   /* 8.750 */
    std::snprintf(l, sizeof l, "  float_add(2.500, 3.500) = %s", sa); line(l);
    std::snprintf(l, sizeof l, "  float_mul(2.500, 3.500) = %s", sm); line(l);

    dlclose(h);
    line("RESULT: SUCCESS");
    for (;;) { }
}
