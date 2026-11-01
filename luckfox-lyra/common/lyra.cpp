/** @file lyra.cpp @brief See lyra.hpp: boot, SMP, semihosting, the loader lock and the DRAM
 *  "flash" the XIP loaders run from. */
#include "lyra.hpp"

#include <cmsis-plus/rtos/os.h>

#include <exception_handler.hpp>
#include <hw_result.hpp>
#include <led.hpp>
#include <smp.hpp>
#include <test-smp-boot.hpp>
#include <uart.hpp>

#include "scl/cache.h"
#include "scl/os-hooks.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace os::rtos;
using u32 = std::uint32_t;

extern "C" {
unsigned port_cpu_id(void);
}

namespace {
bool g_loaded;
volatile unsigned g_done[3], g_bad;
volatile unsigned g_walked;
thread::stack::element_t g_stacks[3][4096];

// ---- semihosting: the Lyra uses the AArch32 Angel trap, SVC 0x123456 -------------------
unsigned semi(unsigned op, const void* arg) {
    register unsigned r0 __asm__("r0") = op;
    register const void* r1 __asm__("r1") = arg;
    __asm__ volatile("svc 0x123456" : "+r"(r0) : "r"(r1) : "memory", "lr");
    return r0;
}
}  // namespace

void say(const char* s) {
    uart::uart1.puts_uart(s);
    static int tt = -1;
    const u32 n = static_cast<u32>(std::strlen(s));
    if (tt < 0) {
        u32 open_args[3] = {reinterpret_cast<u32>(":tt"), 4u, 3u};
        tt = static_cast<int>(semi(0x01u, open_args));
    }
    if (tt < 0) return;
    u32 write_args[3] = {static_cast<u32>(tt), reinterpret_cast<u32>(s), n};
    semi(0x05u, write_args);   // SYS_WRITE: one block, not one byte per trap
}

void sayf(const char* fmt, ...) {
    char l[160];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(l, sizeof l, fmt, ap);
    va_end(ap);
    say(l);
}

/* The user LED, so a run can be read without a console: the boot path blinks it once
 * per core as that core starts, and leaves it on for PASS or blinking for FAIL. */
void blink_led(unsigned times) {
    for (unsigned i = 0; i < times; ++i) {
        led::on();
        sysclock.sleep_for(80);
        led::off();
        sysclock.sleep_for(80);
    }
}

// ---- reading a file from the host ------------------------------------------------------
namespace {
alignas(8) std::byte g_files[8][32 * 1024];
unsigned g_file_count;
}  // namespace

extern "C" std::uint8_t _simage_arena_start[];
std::uint8_t* example_arena(unsigned slot) { return _simage_arena_start + slot * kArenaBytes; }

std::span<const std::byte> read_host_file(const char* name) {
    char path[256];
    std::snprintf(path, sizeof path, "%s/%s", EXAMPLE_FIXTURE_DIR, name);
    u32 open_args[3] = {reinterpret_cast<u32>(path), 1u, static_cast<u32>(std::strlen(path))};
    const int h = static_cast<int>(semi(0x01u, open_args));
    if (h < 0 || g_file_count >= 8) return {};
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

// ---- "flash": there is none for apps here, so images run in place from DRAM -------------
namespace {
constexpr u32 kImageSlot = 64u * 1024u;
alignas(4096) std::byte g_image_region[4 * kImageSlot];
}  // namespace

std::uint32_t flash_address(unsigned slot) {
    return reinterpret_cast<u32>(g_image_region) + slot * kImageSlot;
}

std::span<const std::byte> write_to_flash(std::span<std::byte> image, std::uint32_t flash_addr) {
    if (image.empty()) return {};
    std::memcpy(reinterpret_cast<void*>(flash_addr), image.data(), image.size());
    scl_cache_invalidate(reinterpret_cast<const void*>(flash_addr), image.size());
    return {reinterpret_cast<const std::byte*>(flash_addr), image.size()};
}

unsigned flash_parks(unsigned) { return 0; }

// ---- SCL hooks: a recursive lock across all cores, and cache maintenance ----------------
extern "C" {
namespace {
volatile u32 kLoaderLock;
volatile int g_lock_owner = -1;
unsigned g_lock_depth;
u32 g_lock_cpsr;

inline u32 irq_off() {
    u32 cpsr;
    __asm__ volatile("mrs %0, cpsr\n cpsid i" : "=r"(cpsr) : : "memory");
    return cpsr;
}
inline void irq_restore(u32 cpsr) {
    if (!(cpsr & 0x80u)) __asm__ volatile("cpsie i" : : : "memory");
}
inline void spin_take(volatile u32& l) {
    while (__atomic_exchange_n(&l, 1u, __ATOMIC_ACQUIRE) != 0u) {
    }
}
inline void spin_give(volatile u32& l) { __atomic_store_n(&l, 0u, __ATOMIC_RELEASE); }

/* Code is written as data, so clean it to the point of unification and drop stale
 * instructions. Inner-shareable: the other cores' caches are covered. */
void clean_to_pou(const void* addr, std::size_t len) {
    constexpr u32 kLine = 64u;
    const u32 end = reinterpret_cast<u32>(addr) + len;
    for (u32 a = reinterpret_cast<u32>(addr) & ~(kLine - 1u); a < end; a += kLine)
        __asm__ volatile("mcr p15, 0, %0, c7, c11, 1" : : "r"(a) : "memory");   // DCCMVAU
    __asm__ volatile("dsb ish" : : : "memory");
}
void invalidate_icache() {
    __asm__ volatile("mcr p15, 0, %0, c7, c1, 0\n mcr p15, 0, %0, c7, c1, 6\n dsb ish\n isb"
                     : : "r"(0u) : "memory");   // ICIALLUIS, BPIALLIS
}
}  // namespace

void scl_loader_lock(void) {
    const u32 cpsr = irq_off();
    const int me = static_cast<int>(port_cpu_id());
    if (g_lock_owner != me) {
        spin_take(kLoaderLock);
        g_lock_owner = me;
        g_lock_cpsr = cpsr;
    }
    ++g_lock_depth;
}
void scl_loader_unlock(void) {
    if (--g_lock_depth == 0) {
        const u32 cpsr = g_lock_cpsr;
        g_lock_owner = -1;
        spin_give(kLoaderLock);
        irq_restore(cpsr);
    }
}
void scl_cache_clean(const void* addr, std::size_t len) { clean_to_pou(addr, len); }
void scl_cache_invalidate(const void* addr, std::size_t len) { clean_to_pou(addr, len); invalidate_icache(); }
void scl_log_puts(const char* s) { uart::uart1.puts(s); }
void scl_log_putc(char c) { const char l[2] = {c, 0}; uart::uart1.puts(l); }
}  // extern "C"

// ---- calling into a library ---------------------------------------------------------------
/* call_lib is in arena.S: an exception unwinds through it, so it carries unwind info. */
int lib_call(const Lib& lib, void* fn, std::uintptr_t a, std::uintptr_t b, std::uintptr_t c, std::uintptr_t d) {
    return static_cast<int>(call_lib(fn, lib.got, a, b, c, d));
}
float lib_callf(const Lib& lib, void* fn, float a, float b) {
    using f = float (*)(void*, u32, float, float, std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t);
    return reinterpret_cast<f>(reinterpret_cast<void*>(&call_lib))(fn, lib.got, a, b, 0, 0, 0, 0);
}

// ---- helpers ------------------------------------------------------------------------------
Fn lib_fn(const Lib& lib, const char* name) {
    void* p = lib_sym(lib, name);
    if (!p) {
        say("  FAIL: missing export ");
        say(name);
        say("\n");
        g_bad = g_bad + 1u;
    }
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
    say("  missing import: ");
    say(missing);
    say("\n");
    return false;
}
__attribute__((weak)) void app_together(unsigned) {}

void expect(bool ok, const char* what) {
    if (ok) return;
    say("  FAIL: ");
    say(what);
    say("\n");
    g_bad = g_bad + 1u;
}

// ---- boot: load first, then let the kernel start -------------------------------------------
namespace {
void* worker(void* arg) {
    const unsigned core = static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(arg));
    this_thread::thread().cpu_affinity(1u << core);
    if (port_cpu_id() != core) this_thread::yield();
    while (g_walked != core) sysclock.sleep_for(5);   // one core prints at a time
    blink_led(core + 1u);                             // core 0 once, core 1 twice, core 2 three times
    sayf("\n--- on core %u ---\n", port_cpu_id());
    expect(port_cpu_id() == core, "running on the right core");
    app_on_core(port_cpu_id());
    __asm__ volatile("dmb" : : : "memory");
    g_walked = core + 1u;
    while (g_walked < 3u) sysclock.sleep_for(5);
    app_together(core);
    g_done[core] = 1;
    return nullptr;
}
}  // namespace

int os_main(int, char*[]) {
    (void)os::rtos::interrupts::uncritical_section::enter();
    this_thread::thread().cpu_affinity(1u << 0);
    led::init();
    blink_led(2);   // the firmware is up, before the other cores are released
    say("\n");
    say(kAppName);
    say(": libraries loaded at boot on core 0\n");
    if (!g_loaded || !app_start()) {
        say("LOAD FAILED\nRESULT: FAIL\n");
        blink_led(10);
        hw_result::fail();
    }
    say("  releasing the other cores\n");
    smp_install_boot_threads();
    smp::start_secondary_cores();
    const int waited = test_wait_secondaries(3000);
    sayf("  joined after %d ms\n", waited);
    expect(test_secondaries_joined(), "every core joined");

    static thread* threads[3];
    for (unsigned core = 0; core < 3; ++core) {
        thread::attributes attr = thread::initializer;
        attr.th_stack_address = g_stacks[core];
        attr.th_stack_size_bytes = sizeof g_stacks[core];
        char name[8];
        std::snprintf(name, sizeof name, "wrk%u", core);
        threads[core] = new thread(name, worker, reinterpret_cast<void*>(static_cast<std::uintptr_t>(core)), attr);
        threads[core]->cpu_affinity(1u << core);
    }

    for (unsigned t = 0; t < 1600 && !(g_done[0] && g_done[1] && g_done[2]); ++t) sysclock.sleep_for(100);
    expect(g_done[0] && g_done[1] && g_done[2], "all cores finished");
    const bool ok = g_bad == 0;
    say(ok ? "\nRESULT: PASS\n" : "\nRESULT: FAIL\n");
    if (ok) {
        led::on();          // steady on: PASS
        hw_result::ok();
    } else {
        blink_led(10);      // blinking, then out: FAIL
    }
    hw_result::fail();
    return 0;
}

// ---- start-up: load at boot, on core 0, before the kernel starts ---------------------------
extern "C" {
extern char __heap_start[], __heap_end[], __fiq_stack_top[], __irq_stack_top[];
void os_startup_initialize_hardware_early(void) {}
void os_startup_initialize_hardware(void) {
    uart::uart1.init();
    os_startup_initialize_free_store(__heap_start, static_cast<std::size_t>(__heap_end - __heap_start));
    exception::init();
}
[[noreturn]] static void main_trampoline(void) { std::exit(os_main(0, nullptr)); }
extern void os_startup_create_thread_idle(void);
extern os::rtos::thread* os_main_thread;
int main(int, char*[]) {
    g_loaded = app_load();   // every static constructor has run; the other cores are still parked
    if (g_loaded) scl_publish_release();
#if defined(OS_HAS_INTERRUPTS_STACK)
    interrupts::stack()->set(reinterpret_cast<thread::stack::element_t*>(__fiq_stack_top), __irq_stack_top - __fiq_stack_top);
    interrupts::stack()->initialize();
#endif
    scheduler::initialize();
    static thread::stack::element_t main_stack[8192];
    thread::attributes attr = thread::initializer;
    attr.th_stack_address = main_stack;
    attr.th_stack_size_bytes = sizeof main_stack;
    static thread main_thread{"main", reinterpret_cast<thread::func_t>(main_trampoline), nullptr, attr};
    os_main_thread = &main_thread;
    os_startup_create_thread_idle();
    scheduler::start();
    return 0;
}
}

// ---- newlib's file calls: scl::static's log pulls them in, and nothing here uses files ----
extern "C" {
__attribute__((weak)) int _close(int) { return -1; }
__attribute__((weak)) int _fstat(int, void*) { return -1; }
__attribute__((weak)) int _isatty(int) { return 0; }
__attribute__((weak)) int _lseek(int, int, int) { return -1; }
__attribute__((weak)) int _read(int, void*, unsigned) { return -1; }
__attribute__((weak)) int _write(int, const void*, unsigned) { return -1; }
}
