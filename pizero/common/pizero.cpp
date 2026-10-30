/** @file pizero.cpp @brief See pizero.hpp. */
#include "pizero.hpp"

#include <cmsis-plus/rtos/os.h>

#include <bsp/multicore.hpp>
#include <bsp/uart.hpp>

#include <hw_result.hpp>

#include "scl/os-hooks.h"
#include "scl/cache.h"

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
float app_gain(void) { return 1.5f; }
}

namespace os::rtos::scheduler {
extern thread* os_idle_thread_core[OS_NCPU];
}

const scl_sym_entry kMainSymbols[] = {
    {"app_gain", reinterpret_cast<u32>(&app_gain)},
};
const std::size_t kMainSymbolCount = 1;

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
alignas(8) std::byte g_files[4][16 * 1024];
unsigned g_file_count;
}  // namespace

extern "C" std::uint8_t _simage_arena_start[];
void* example_arena() { return _simage_arena_start; }

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

// ---- writing a provisioned image to flash, through the bootrom ---------------------------
namespace {
constexpr u32 kXipBase = 0x10000000u, kFlashOffset = 8u * 1024u * 1024u, kSector = 4096u;
using rom_void = void (*)(void);
using rom_erase = void (*)(u32, std::size_t, u32, std::uint8_t);
using rom_prog = void (*)(u32, const std::uint8_t*, std::size_t);
struct Rom { rom_void connect, exit_xip, flush, enter_xip; rom_erase erase; rom_prog prog; } g_rom;

void* rom_lookup(char a, char b) {
    using lookup_t = void* (*)(u32, u32);
    const auto fn = reinterpret_cast<lookup_t>(
        static_cast<std::uintptr_t>(*reinterpret_cast<const std::uint16_t*>(0x16u)));
    return fn(static_cast<u32>(a) | (static_cast<u32>(b) << 8), 0x0004u);
}

/* Runs from SRAM: flash is not readable between exit_xip and enter_xip. */
__attribute__((section(".data.flash_write"), noinline, long_call)) void
flash_write_ram(u32 offset, const std::uint8_t* data, std::size_t n) {
    g_rom.connect();
    g_rom.exit_xip();
    g_rom.erase(offset, (n + kSector - 1u) & ~(kSector - 1u), kSector, 0x20u);
    g_rom.prog(offset, data, (n + 255u) & ~255u);
    g_rom.flush();
    g_rom.enter_xip();
}
}  // namespace

std::uint32_t flash_address(unsigned slot) { return kXipBase + kFlashOffset + slot * 64u * 1024u; }

std::span<const std::byte> write_to_flash(std::span<std::byte> image, std::uint32_t flash_addr) {
    g_rom = Rom{reinterpret_cast<rom_void>(rom_lookup('I', 'F')),
                reinterpret_cast<rom_void>(rom_lookup('E', 'X')),
                reinterpret_cast<rom_void>(rom_lookup('F', 'C')),
                reinterpret_cast<rom_void>(rom_lookup('C', 'X')),
                reinterpret_cast<rom_erase>(rom_lookup('R', 'E')),
                reinterpret_cast<rom_prog>(rom_lookup('R', 'P'))};
    __asm__ volatile("cpsid i" : : : "memory");
    flash_write_ram(flash_addr - kXipBase, reinterpret_cast<const std::uint8_t*>(image.data()), image.size());
    __asm__ volatile("cpsie i" : : : "memory");
    const auto* in_flash = reinterpret_cast<const std::byte*>(flash_addr);
    if (std::memcmp(in_flash, image.data(), image.size()) != 0) return {};
    return {in_flash, image.size()};
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

// ---- boot: load first, then let the kernel start -----------------------------------------
namespace {
FloatLib g_lib;
bool g_loaded;
volatile unsigned g_done[2], g_bad;
thread::stack::element_t g_stacks[2][1024], g_idle_stack[512];

/* Enter the library with r10 = its GOT base. Floats pass through in s0, s1. */
__attribute__((naked)) float call_lib(float_fn, u32 /*got*/, float, float) {
    __asm__ volatile("push {r10, lr}\n mov r10, r1\n blx r0\n pop {r10, pc}\n");
}

void show(const char* what, float got, float want) {
    char l[80];
    const unsigned milli = static_cast<unsigned>(got * 1000.0f + 0.5f);
    std::snprintf(l, sizeof l, "  %s = %u.%03u\n", what, milli / 1000u, milli % 1000u);
    say(l);
    const float d = got - want;
    if (d > 0.001f || d < -0.001f) g_bad = g_bad + 1u;
}

void* worker(void* arg) {
    const unsigned core = static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(arg));
    this_thread::thread().cpu_affinity(1u << core);
    if (port_cpu_id() != core) this_thread::yield();
    while (core == 1u && !g_done[0]) sysclock.sleep_for(5);   // one core prints at a time
    char l[48];
    std::snprintf(l, sizeof l, "\n--- calls on core %u ---\n", port_cpu_id());
    say(l);
    show("float_add(2.5, 3.5)", call_lib(g_lib.add, g_lib.got, 2.5f, 3.5f), 6.0f);
    if (g_lib.mul) show("float_mul(2.5, 3.5)", call_lib(g_lib.mul, g_lib.got, 2.5f, 3.5f), 8.75f);
    show("float_scale(2, 3) [imports app_gain from main]", call_lib(g_lib.scale, g_lib.got, 2.0f, 3.0f), 9.0f);
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

extern "C" int __real_main(int, char**);
extern "C" int __wrap_main(int argc, char** argv) {
    g_loaded = example_load(g_lib);   // all static constructors have run; SMP has not started
    return __real_main(argc, argv);
}

int os_main(int, char*[]) {
    this_thread::thread().cpu_affinity(1u << 0);
    g_uart_ready = true;
    say("\n");
    say(kExampleName);
    say(": library loaded at boot on core 0, now starting SMP\n");
    if (!g_loaded) {
        say("LOAD FAILED\nRESULT: FAIL\n");
        hw_result::fail();
    }
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
    for (unsigned t = 0; t < 100 && !(g_done[0] && g_done[1]); ++t) sysclock.sleep_for(100);
    const bool ok = g_done[0] && g_done[1] && g_bad == 0;
    say(ok ? "\nRESULT: PASS\n" : "\nRESULT: FAIL\n");
    if (ok) hw_result::ok();
    hw_result::fail();
    return 0;
}
