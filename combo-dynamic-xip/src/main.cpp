/**
 * @file main.cpp — combo-dynamic-xip: float module -> unload -> spi module, all
 *        via the POSIX dlopen API, executing IN PLACE from flash (XIP + r10).
 *
 * Model F/XIP (import_in_project_full.md): scl::xip-dynamic-newlib. dlopen
 * floatlib.so (XIP), compute sum/prod, KEEP them in main, dlclose; then dlopen
 * spifs.so (XIP), write the kept values to a file on the on-board W25Q64 flat-fs
 * and read them back. Each module runs its R-X from flash; r10 is set to the
 * live module's GOT base per call. Output on USART1 (PA9, 115200) + semihosting.
 * WeAct F411CE.
 */
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <span>
#include "stm32f4xx_hal.h"
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

extern "C" const std::uint8_t scl_symtab_blob[];
constexpr std::uint32_t kSymtabMagic = 0x534C4353;   /* 'SCLS' */

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
            xip::newlib::patch_main_imports(std::span<std::byte>{buf, sz});
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

/* r10-aware call: set r10 = the live module's GOT base, then call the pointer */
static std::uint32_t g_got = 0;
template <class Fn, class... A>
static auto rcall(void* h, const char* name, A... args) {
    Fn fn = reinterpret_cast<Fn>(dlsym(h, name));
    r10_val = reinterpret_cast<void*>(g_got);
    return fn(args...);
}

int main(void) {
    HAL_Init();
    uart_init();
    line("combo-dynamic-xip: float -> unload -> spi (POSIX dlopen, XIP+r10)");
    if (!load_symtab()) { line("RESULT: FAILURE"); for (;;) {} }
    fw::fs_format();

    /* ===== 1) float module (XIP), compute, keep values ===== */
    float sum = 0, prod = 0;
    {
        void* h = dlopen(SCL_SO_DIR "/floatlib.so", 0);
        if (!h) { line("  float dlopen failed"); line("RESULT: FAILURE"); for (;;) {} }
        g_got = xip::newlib::get_got_base(h);
        sum  = rcall<float(*)(float,float)>(h, "float_add", 2.5f, 3.5f);   /* 6.000 */
        prod = rcall<float(*)(float,float)>(h, "float_mul", 2.5f, 3.5f);   /* 8.750 */
        char sa[16], sm[16], l[80]; fstr(sum, sa); fstr(prod, sm);
        std::snprintf(l, sizeof l, "  floatlib: sum=%s prod=%s", sa, sm); line(l);
        dlclose(h);                                                       /* ===== 2) unload ===== */
        line("  float module unloaded");
    }

    /* ===== 3) spi module (XIP), persist the kept values ===== */
    {
        void* h = dlopen(SCL_SO_DIR "/spifs.so", 0);
        if (!h) { line("  spi dlopen failed"); line("RESULT: FAILURE"); for (;;) {} }
        g_got = xip::newlib::get_got_base(h);
        if (rcall<int(*)(void)>(h, "spifs_init") != 0) { line("  W25Q64 NOT found -- check SPI wiring"); for (;;) {} }
        line("  spifs.so loaded XIP; W25Q64 up");
        rcall<int(*)(void)>(h, "spifs_format");

        char sa[16], sm[16], body[64]; fstr(sum, sa); fstr(prod, sm);
        int k = std::snprintf(body, sizeof body, "sum=%s prod=%s\n", sa, sm);
        rcall<int(*)(const char*, const void*, unsigned)>(h, "spifs_create", "calc.txt", body, (unsigned)k);
        line("  wrote calc.txt to the SPI flatfs");

        char buf[64]; unsigned got = 0;
        if (rcall<int(*)(const char*, void*, unsigned, unsigned*)>(h, "spifs_read", "calc.txt", buf, sizeof buf - 1, &got) == 0) {
            if (got >= sizeof buf) got = sizeof buf - 1; buf[got] = 0;
            for (unsigned j = 0; j < got; ++j) if (buf[j] == '\n') buf[j] = 0;
            char l[96]; std::snprintf(l, sizeof l, "  read back calc.txt: \"%s\"", buf); line(l);
        }
        dlclose(h);
    }

    line("combo-dynamic-xip: done");
    line("RESULT: SUCCESS");
    for (;;) { }
}
