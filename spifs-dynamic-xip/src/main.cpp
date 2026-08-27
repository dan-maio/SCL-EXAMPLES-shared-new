/**
 * @file main.cpp — spifs-dynamic-xip: drive the on-board W25Q64 flat-fs via a
 *        module executed IN PLACE from flash (XIP + r10), loaded by dlopen.
 *
 * Model F/XIP (import_in_project_full.md): scl::xip-dynamic-newlib. spifs.so is
 * an r10-based split .so; its R-X segment runs from flash, only the RW/GOT goes
 * to RAM. POSIX surface: dlopen provisions the .so into the on-chip flat-fs and
 * XIP-loads it; its HAL imports resolve via the embedded symtab. Each call sets
 * r10 to the module GOT base first (rcall helper). Output on USART1 (PA9,
 * 115200) + the semihosting console. WeAct F411CE.
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

register void* r10_val asm("r10");                   /* the module GOT base register */

static void line(const char* s) { uart_puts(s); puts(s); }

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

/* r10-aware call: set r10 = the module GOT base, then call the typed pointer */
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
    line("spifs-dynamic-xip: POSIX dlopen of spifs.so (XIP+r10 / scl::xip-dynamic-newlib)");
    if (!load_symtab()) { line("RESULT: FAILURE"); for (;;) {} }
    fw::fs_format();

    void* h = dlopen(SCL_SO_DIR "/spifs.so", 0);
    if (!h) { line("  dlopen failed"); line("RESULT: FAILURE"); for (;;) {} }
    g_got = xip::newlib::get_got_base(h);
    line("  loaded XIP (code from flash)");

    if (rcall<int(*)(void)>(h, "spifs_init") != 0) { line("  W25Q64 NOT found -- check SPI wiring"); for (;;) {} }
    line("  W25Q64 detected, SPI up");
    rcall<int(*)(void)>(h, "spifs_format");
    line("  flatfs formatted");

    rcall<int(*)(const char*, const void*, unsigned)>(h, "spifs_create", "hello.txt", "Hello WeAct!\n", 13);
    rcall<int(*)(const char*, const void*, unsigned)>(h, "spifs_create", "nums.txt",  "1 2 3 4 5\n",    10);
    rcall<int(*)(const char*, const void*, unsigned)>(h, "spifs_create", "scl.txt",   "loaded by name\n", 15);
    char l[96];
    std::snprintf(l, sizeof l, "  created 3 files; count=%u", rcall<unsigned(*)(void)>(h, "spifs_count")); line(l);

    line("  -- ls --");
    for (unsigned i = 0; ; ++i) {
        char nm[32]; unsigned sz = 0;
        if (rcall<int(*)(unsigned, char*, unsigned*)>(h, "spifs_list", i, nm, &sz) != 0) break;
        std::snprintf(l, sizeof l, "    %s (%uB)", nm, sz); line(l);
    }
    line("  -- cat --");
    const char* files[] = { "hello.txt", "nums.txt", "scl.txt" };
    for (const char* fn : files) {
        char buf[64]; unsigned got = 0;
        if (rcall<int(*)(const char*, void*, unsigned, unsigned*)>(h, "spifs_read", fn, buf, sizeof buf - 1, &got) == 0) {
            if (got >= sizeof buf) got = sizeof buf - 1; buf[got] = 0;
            for (unsigned j = 0; j < got; ++j) if (buf[j] == '\n') buf[j] = 0;
            std::snprintf(l, sizeof l, "    %s: \"%s\"", fn, buf); line(l);
        }
    }
    rcall<int(*)(const char*)>(h, "spifs_delete", "hello.txt");
    std::snprintf(l, sizeof l, "  deleted hello.txt; count=%u", rcall<unsigned(*)(void)>(h, "spifs_count")); line(l);

    dlclose(h);
    line("RESULT: SUCCESS");
    for (;;) { }
}
