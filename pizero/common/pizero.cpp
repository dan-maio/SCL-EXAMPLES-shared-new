/** @file pizero.cpp @brief See pizero.hpp. */
#include "pizero.hpp"

#include <cmsis-plus/rtos/os.h>

#include <bsp/led.hpp>
#include <bsp/multicore.hpp>
#include <bsp/uart.hpp>

#include <hw_result.hpp>

#include "scl/os-hooks.h"
#include "scl/cache.h"
#include "scl/rp2350-flash.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace os::rtos;
using u32 = std::uint32_t;

extern "C" {
void port_smp_secondary_start(void);
unsigned port_core1_stack_top(void);
unsigned port_cpu_id(void);
void port_smp_core0_ipi_enable(void);
int board_flash_read_id(std::uint8_t* mid, std::uint8_t* tid);
void board_flash_erase_sector(std::uint32_t addr);
int board_flash_program(std::uint32_t addr, const std::uint8_t* data, std::uint32_t len);
const std::uint8_t* board_flash_map(std::uint32_t addr);
}

namespace os::rtos::scheduler {
extern thread* os_idle_thread_core[OS_NCPU];
}

namespace { bool g_uart_ready; }   // it comes up with the kernel's main

void say(const char* s) {
    if (g_uart_ready) uart::write(s);
    if (!uos_debug_host_attached()) return;
    register unsigned r0 __asm__("r0") = 0x04u;  // SYS_WRITE0
    register const char* r1 __asm__("r1") = s;
    __asm__ volatile("bkpt 0xAB" : "+r"(r0) : "r"(r1) : "memory");
}

// ---- reading a file from the host ------------------------------------------------------
namespace {
unsigned semi(unsigned op, const void* arg) {
    register unsigned r0 __asm__("r0") = op;
    register const void* r1 __asm__("r1") = arg;
    __asm__ volatile("bkpt 0xAB" : "+r"(r0) : "r"(r1) : "memory");
    return r0;
}
alignas(8) std::byte g_files[4][32 * 1024];
unsigned g_file_count;
}  // namespace

extern "C" std::uint8_t _simage_arena_start[];
std::uint8_t* example_arena(unsigned slot) { return _simage_arena_start + slot * kArenaBytes; }

std::span<const std::byte> read_host_file(const char* name) {
    char path[200];
    std::snprintf(path, sizeof path, "%s/%s", EXAMPLE_FIXTURE_DIR, name);
    u32 open_args[3] = {reinterpret_cast<u32>(path), 1u, static_cast<u32>(std::strlen(path))};
    const int h = static_cast<int>(semi(0x01u, open_args));
    if (h < 0 || g_file_count >= 4) return {};
    std::byte* buf = g_files[g_file_count];
    u32 handle = static_cast<u32>(h);
    const std::size_t n = semi(0x0Cu, &handle);
    std::span<const std::byte> out;
    if (n > 0 && n <= sizeof g_files[0]) {
        u32 read_args[3] = {handle, reinterpret_cast<u32>(buf), static_cast<u32>(n)};
        if (semi(0x06u, read_args) == 0) { out = {buf, n}; ++g_file_count; }
    }
    semi(0x02u, &handle);
    return out;
}

// ---- flash: scl::rp2350-flash does the work ------------------------------------------------
namespace {
constexpr u32 kXipBase = 0x10000000u, kFlashOffset = 8u * 1024u * 1024u;
constexpr u32 kFsBase = 9u * 1024u * 1024u;   // the w25q64 driver's flash, away from the images
}  // namespace

unsigned flash_parks(unsigned core) { return scl_rp2350_flash_parks(core); }

std::uint32_t flash_address(unsigned slot) { return kXipBase + kFlashOffset + slot * 64u * 1024u; }

std::span<const std::byte> write_to_flash(std::span<std::byte> image, std::uint32_t flash_addr) {
    const u32 off = flash_addr - kXipBase, sectors = (image.size() + SCL_RP2350_FLASH_SECTOR - 1u) & ~(SCL_RP2350_FLASH_SECTOR - 1u);
    if (!scl_rp2350_flash_erase(off, sectors) || !scl_rp2350_flash_program(off, image.data(), image.size())) return {};
    return {reinterpret_cast<const std::byte*>(flash_addr), image.size()};
}

// ---- the flash the w25q64 driver in the libraries uses -------------------------------------
extern "C" {
const std::uint8_t* board_flash_map(std::uint32_t addr) {
    return static_cast<const std::uint8_t*>(scl_rp2350_flash_map(kFsBase + addr));
}
int board_flash_read_id(std::uint8_t* mid, std::uint8_t* tid) {
    std::uint8_t id[3];
    scl_rp2350_flash_read_id(id);
    char l[48];
    std::snprintf(l, sizeof l, "  flash JEDEC id: %02x %02x %02x\n", id[0], id[1], id[2]);
    say(l);
    *mid = id[0];
    *tid = id[1];
    return id[0] != 0x00u && id[0] != 0xFFu;
}
void board_flash_erase_sector(std::uint32_t addr) { scl_rp2350_flash_erase(kFsBase + addr, SCL_RP2350_FLASH_SECTOR); }
int board_flash_program(std::uint32_t addr, const std::uint8_t* data, std::uint32_t len) {
    return scl_rp2350_flash_program(kFsBase + addr, data, len) ? 1 : 0;
}
}

// ---- SCL hooks: a recursive lock across both cores ---------------------------------------
extern "C" {
namespace {
constexpr u32 kLoaderLock = 0xD0000100u + 29u * 4u;
volatile int g_owner = -1;
unsigned g_depth;
u32 g_primask;
}  // namespace
void scl_loader_lock(void) {
    u32 pm;
    __asm__ volatile("mrs %0, primask\n cpsid i" : "=r"(pm) : : "memory");
    const int me = static_cast<int>(port_cpu_id());
    if (g_owner != me) {
        while (*reinterpret_cast<volatile u32*>(kLoaderLock) == 0) {
        }
        __asm__ volatile("dmb" : : : "memory");
        g_owner = me;
        g_primask = pm;
    }
    ++g_depth;
}
void scl_loader_unlock(void) {
    if (--g_depth == 0) {
        const u32 pm = g_primask;
        g_owner = -1;
        __asm__ volatile("dmb" : : : "memory");
        *reinterpret_cast<volatile u32*>(kLoaderLock) = 1;
        __asm__ volatile("msr primask, %0" : : "r"(pm) : "memory");
    }
}
void scl_log_puts(const char* s) { say(s); }
void scl_log_putc(char c) { const char l[2] = {c, 0}; say(l); }
}

// ---- calling into a library ---------------------------------------------------------------
/* call_lib is in arena.S: an exception unwinds through it, so it carries unwind info. */
int lib_call(const Lib& lib, void* fn, std::uintptr_t a, std::uintptr_t b, std::uintptr_t c, std::uintptr_t d) {
    return static_cast<int>(call_lib(fn, lib.got, a, b, c, d));
}
float lib_callf(const Lib& lib, void* fn, float a, float b) {
    using f = float (*)(void*, u32, float, float, std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t);
    return reinterpret_cast<f>(reinterpret_cast<void*>(&call_lib))(fn, lib.got, a, b, 0, 0, 0, 0);
}

// ---- boot: load first, then let the kernel start -----------------------------------------
namespace {
bool g_loaded;
volatile unsigned g_done[2], g_bad;
thread::stack::element_t g_stacks[2][2048], g_idle_stack[512];
volatile unsigned g_walked;
volatile bool g_ready_core[2];

void* worker(void* arg) {
    const unsigned core = static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(arg));
    this_thread::thread().cpu_affinity(1u << core);
    if (port_cpu_id() != core) this_thread::yield();
    while (g_walked != core) sysclock.sleep_for(5);   // one core prints at a time
    sayf("\n--- on core %u ---\n", port_cpu_id());
    expect(port_cpu_id() == core, "running on the right core");
    app_on_core(port_cpu_id());
    __asm__ volatile("dmb" : : : "memory");
    g_walked = core + 1u;
    while (g_walked < 2u) sysclock.sleep_for(5);
    if (app_writes_flash_under_smp()) scl_rp2350_flash_park_enable();
    g_ready_core[core] = true;
    __asm__ volatile("dmb" : : : "memory");
    while (!g_ready_core[0] || !g_ready_core[1]) sysclock.sleep_for(5);
    app_together(core);
    g_done[core] = 1;
    return nullptr;
}

void* idle1(void*) {
    this_thread::thread().priority(thread::priority::idle);
    for (;;) {
        __asm__ volatile("dsb\n wfi" : : : "memory");
        this_thread::yield();
    }
    return nullptr;
}
}  // namespace

Fn lib_fn(const Lib& lib, const char* name) {
    void* p = lib_sym(lib, name);
    if (!p) { say("  FAIL: missing export "); say(name); say("\n"); g_bad = g_bad + 1u; }
    return Fn{p, lib.got};
}
void show(const char* what, std::uint32_t v) {
    char l[120];
    std::snprintf(l, sizeof l, "  %s = %u\n", what, static_cast<unsigned>(v));
    say(l);
}
void show(const char* what, std::uint32_t v, std::uint32_t want) {
    show(what, v);
    expect(v == want, what);
}
__attribute__((weak)) bool lib_load_set(Lib* libs, const char* const* bases, unsigned n) {
    for (unsigned i = 0; i < n; ++i)
        if (!lib_load(libs[i], i, bases[i])) return false;
    return true;
}
bool libs_ready() {
    const char* missing = lib_first_missing();
    if (!missing) return true;
    say("  missing import: "); say(missing); say("\n");
    return false;
}
__attribute__((weak)) void app_together(unsigned) {}
__attribute__((weak)) bool app_writes_flash_under_smp() { return false; }

void sayf(const char* fmt, unsigned a, unsigned b, const char* s) {
    char l[160];
    std::snprintf(l, sizeof l, fmt, a, b, s);
    say(l);
}

void expect(bool ok, const char* what) {
    if (ok) return;
    say("  FAIL: ");
    say(what);
    say("\n");
    g_bad = g_bad + 1u;
}

extern "C" int __real_main(int, char**);
extern "C" int __wrap_main(int argc, char** argv) {
    g_loaded = app_load();   // all static constructors have run; SMP has not started
    return __real_main(argc, argv);
}

int os_main(int, char*[]) {
    this_thread::thread().cpu_affinity(1u << 0);
    led::init();
    g_uart_ready = true;
    say("\n");
    say(kAppName);
    say(": libraries loaded at boot on core 0\n");
    if (!g_loaded || !app_start()) {
        say("LOAD FAILED\nRESULT: FAIL\n");
        hw_result::fail();
    }
    if (app_writes_flash_under_smp()) scl_rp2350_flash_smp_init();
    say("  starting SMP\n");
    {
        thread::attributes attr = thread::initializer;
        attr.th_stack_address = g_idle_stack;
        attr.th_stack_size_bytes = sizeof g_idle_stack;
        scheduler::os_idle_thread_core[1] = new thread("idle1", idle1, nullptr, attr);
    }
    for (unsigned core = 0; core < 2; ++core) {
        thread::attributes attr = thread::initializer;
        attr.th_stack_address = g_stacks[core];
        attr.th_stack_size_bytes = sizeof g_stacks[core];
        auto* t = new thread("call", worker, reinterpret_cast<void*>(static_cast<std::uintptr_t>(core)), attr);
        t->cpu_affinity(1u << core);
    }
    multicore::launch_core1(port_smp_secondary_start, port_core1_stack_top());
    port_smp_core0_ipi_enable();
    for (unsigned t = 0; t < 1200 && !(g_done[0] && g_done[1]); ++t) sysclock.sleep_for(100);
    expect(g_done[0] && g_done[1], "both cores finished");
    const bool ok = g_bad == 0;
    say(ok ? "\nRESULT: PASS\n" : "\nRESULT: FAIL\n");
    if (ok) hw_result::ok();
    hw_result::fail();
    return 0;
}
