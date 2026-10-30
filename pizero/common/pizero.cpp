/** @file pizero.cpp @brief See pizero.hpp. */
#include "pizero.hpp"

#include <cmsis-plus/rtos/os.h>

#include <bsp/multicore.hpp>
#include <bsp/uart.hpp>

#include <hw_result.hpp>

#include "scl/os-hooks.h"
#include "scl/cache.h"

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

#define SYM(n) {#n, reinterpret_cast<u32>(&n)}
const scl_sym_entry kMainSymbols[] = {
    SYM(board_flash_read_id), SYM(board_flash_erase_sector), SYM(board_flash_program), SYM(board_flash_map),
    SYM(memcpy), SYM(memset), SYM(memcmp), SYM(strlen),
};
const std::size_t kMainSymbolCount = sizeof kMainSymbols / sizeof kMainSymbols[0];

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
alignas(8) std::byte g_files[4][24 * 1024];
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

/* Run from SRAM: flash is not readable between exit_xip and enter_xip. */
__attribute__((section(".data.flash_write"), noinline, long_call)) void
ram_erase(u32 offset, std::size_t n) {
    g_rom.connect();
    g_rom.exit_xip();
    g_rom.erase(offset, n, kSector, 0x20u);
    g_rom.flush();
    g_rom.enter_xip();
}
__attribute__((section(".data.flash_write"), noinline, long_call)) void
ram_program(u32 offset, const std::uint8_t* data, std::size_t n) {
    g_rom.connect();
    g_rom.exit_xip();
    g_rom.prog(offset, data, n);
    g_rom.flush();
    g_rom.enter_xip();
}
/* The JEDEC ID command, through the QMI's direct mode. */
__attribute__((section(".data.flash_write"), noinline, long_call)) void
ram_read_id(std::uint8_t* out) {
    volatile u32* const csr = reinterpret_cast<volatile u32*>(0x400D0000u);
    volatile u32* const tx = csr + 1;
    volatile u32* const rx = csr + 2;
    g_rom.connect();
    g_rom.exit_xip();
    *csr = (30u << 22) | 1u;              // CLKDIV, EN
    while (*csr & 2u) {
    }
    *csr |= 4u;                           // assert CS0
    for (unsigned i = 0; i < 4; ++i) {
        *tx = i == 0 ? 0x9Fu : 0u;
        for (unsigned t = 0; t < 100000u && (*csr & (1u << 16)); ++t) {
        }
        out[i] = static_cast<std::uint8_t>(*rx);
    }
    *csr &= ~4u;
    *csr &= ~1u;
    g_rom.flush();
    g_rom.enter_xip();
}
void rom_init() {
    if (g_rom.connect) return;
    g_rom = Rom{reinterpret_cast<rom_void>(rom_lookup('I', 'F')),
                reinterpret_cast<rom_void>(rom_lookup('E', 'X')),
                reinterpret_cast<rom_void>(rom_lookup('F', 'C')),
                reinterpret_cast<rom_void>(rom_lookup('C', 'X')),
                reinterpret_cast<rom_erase>(rom_lookup('R', 'E')),
                reinterpret_cast<rom_prog>(rom_lookup('R', 'P'))};
}
u32 irq_off() { u32 pm; __asm__ volatile("mrs %0, primask\n cpsid i" : "=r"(pm) : : "memory"); return pm; }
void irq_restore(u32 pm) { __asm__ volatile("msr primask, %0" : : "r"(pm) : "memory"); }
constexpr u32 kFsBase = 9u * 1024u * 1024u;   // the w25q64 driver's flash, away from the images
}  // namespace

std::uint32_t flash_address(unsigned slot) { return kXipBase + kFlashOffset + slot * 64u * 1024u; }

std::span<const std::byte> write_to_flash(std::span<std::byte> image, std::uint32_t flash_addr) {
    rom_init();
    const u32 off = flash_addr - kXipBase;
    const u32 pm = irq_off();
    ram_erase(off, (image.size() + kSector - 1u) & ~(kSector - 1u));
    ram_program(off, reinterpret_cast<const std::uint8_t*>(image.data()), (image.size() + 255u) & ~255u);
    irq_restore(pm);
    const auto* in_flash = reinterpret_cast<const std::byte*>(flash_addr);
    if (std::memcmp(in_flash, image.data(), image.size()) != 0) return {};
    return {in_flash, image.size()};
}

// ---- the flash the w25q64 driver in the libraries uses -------------------------------------
extern "C" {
const std::uint8_t* board_flash_map(std::uint32_t addr) {
    return reinterpret_cast<const std::uint8_t*>(kXipBase + kFsBase + addr);
}
int board_flash_read_id(std::uint8_t* mid, std::uint8_t* tid) {
    rom_init();
    std::uint8_t id[4] = {};
    const u32 pm = irq_off();
    ram_read_id(id);
    irq_restore(pm);
    char l[48];
    std::snprintf(l, sizeof l, "  flash JEDEC id: %02x %02x %02x\n", id[1], id[2], id[3]);
    say(l);
    *mid = id[1];
    *tid = id[2];
    return id[1] != 0x00u && id[1] != 0xFFu;
}
void board_flash_erase_sector(std::uint32_t addr) {
    rom_init();
    const u32 pm = irq_off();
    ram_erase(kFsBase + addr, kSector);
    irq_restore(pm);
}
int board_flash_program(std::uint32_t addr, const std::uint8_t* data, std::uint32_t len) {
    rom_init();
    alignas(4) static std::uint8_t page[256];
    for (std::uint32_t done = 0; done < len;) {
        const std::uint32_t at = addr + done, start = at & ~255u, in = at - start;
        const std::uint32_t n = std::min<std::uint32_t>(256u - in, len - done);
        std::memcpy(page, board_flash_map(start), 256);   // bits already programmed stay
        std::memcpy(page + in, data + done, n);
        const u32 pm = irq_off();
        ram_program(kFsBase + start, page, 256);
        irq_restore(pm);
        if (std::memcmp(board_flash_map(at), data + done, n) != 0) return 0;
        done += n;
    }
    return 1;
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
/* r0 = fn, r1 = got, r2.. = arguments, moved down two registers. Floats stay in s0, s1. */
extern "C" __attribute__((naked)) int call_lib(void*, u32, std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t) {
    __asm__ volatile(
        "push {r4, r5, r10, lr}\n mov r10, r1\n mov r12, r0\n mov r0, r2\n mov r1, r3\n"
        "ldr r2, [sp, #16]\n ldr r3, [sp, #20]\n blx r12\n pop {r4, r5, r10, pc}\n");
}
int lib_call(const Lib& lib, void* fn, std::uintptr_t a, std::uintptr_t b, std::uintptr_t c, std::uintptr_t d) {
    return call_lib(fn, lib.got, a, b, c, d);
}
float lib_callf(const Lib& lib, void* fn, float a, float b) {
    using f = float (*)(void*, u32, float, float, std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t);
    return reinterpret_cast<f>(reinterpret_cast<void*>(&call_lib))(fn, lib.got, a, b, 0, 0, 0, 0);
}

// ---- boot: load first, then let the kernel start -----------------------------------------
namespace {
bool g_loaded;
volatile unsigned g_done[2], g_bad;
thread::stack::element_t g_stacks[2][1024], g_idle_stack[512];

void* worker(void* arg) {
    const unsigned core = static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(arg));
    this_thread::thread().cpu_affinity(1u << core);
    if (port_cpu_id() != core) this_thread::yield();
    while (core == 1u && !g_done[0]) sysclock.sleep_for(5);   // one core prints at a time
    sayf("\n--- on core %u ---\n", port_cpu_id());
    expect(port_cpu_id() == core, "running on the right core");
    app_on_core(port_cpu_id());
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
    g_uart_ready = true;
    say("\n");
    say(kAppName);
    say(": libraries loaded at boot on core 0\n");
    if (!g_loaded || !app_start()) {
        say("LOAD FAILED\nRESULT: FAIL\n");
        hw_result::fail();
    }
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
    for (unsigned t = 0; t < 100 && !(g_done[0] && g_done[1]); ++t) sysclock.sleep_for(100);
    expect(g_done[0] && g_done[1], "both cores finished");
    const bool ok = g_bad == 0;
    say(ok ? "\nRESULT: PASS\n" : "\nRESULT: FAIL\n");
    if (ok) hw_result::ok();
    hw_result::fail();
    return 0;
}
