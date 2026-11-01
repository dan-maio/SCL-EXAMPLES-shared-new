/**
 * @file host-imports.cpp
 * @brief What the fixtures import from main: the same functions the WeAct firmwares give
 * them, made safe across three cores, and the table the loaders resolve them from.
 *
 * The Lyra's storage is the microSD card, not SPI flash, so the flash imports of the
 * WeAct tests are replaced by board_sd_store/board_sd_load. Their weak defaults here keep
 * every firmware linkable; common/board-sd.cpp supplies the real ones for the SD apps.
 */
#include "lyra.hpp"

#include <cmsis-plus/rtos/os.h>
#include <cmsis-plus/estd/memory_resource>

#include <uart.hpp>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#if defined(APP_EH)
#include <new>
#endif

using u32 = std::uint32_t;

extern "C" unsigned port_cpu_id(void);
extern "C" {
float __aeabi_d2f(double);
double __aeabi_f2d(float);
}

namespace {
volatile u32 kAppLock;
u32 g_cpsr[OS_NCPU];
unsigned g_depth[OS_NCPU];

inline u32 irq_off() {
    u32 cpsr;
    __asm__ volatile("mrs %0, cpsr\n cpsid i" : "=r"(cpsr) : : "memory");
    return cpsr;
}
inline void irq_restore(u32 cpsr) {
    if (!(cpsr & 0x80u)) __asm__ volatile("cpsie i" : : : "memory");
}
inline unsigned me() {
    const unsigned id = port_cpu_id();
    return id < OS_NCPU ? id : 0u;
}
}  // namespace

/* Recursive: a fixture called under the lock may allocate, and malloc takes it too. */
void app_lock() {
    const u32 cpsr = irq_off();
    const unsigned core = me();
    if (g_depth[core]++ == 0) {
        while (__atomic_exchange_n(&kAppLock, 1u, __ATOMIC_ACQUIRE) != 0u) {
        }
        g_cpsr[core] = cpsr;
    }
}
void app_unlock() {
    const unsigned core = me();
    if (--g_depth[core] == 0) {
        const u32 cpsr = g_cpsr[core];
        __atomic_store_n(&kAppLock, 0u, __ATOMIC_RELEASE);
        irq_restore(cpsr);
    }
}

// ---- the fixtures' imports -------------------------------------------------------------
extern "C" {
void trace_event(const char* origin, const char* fn, u32 arg) {
    char l[96];
    std::snprintf(l, sizeof l, "  [trace] %s: %s(%u)\n", origin, fn, static_cast<unsigned>(arg));
    say(l);
}
void trace_u32(const char* label, u32 v) {
    char l[96];
    std::snprintf(l, sizeof l, "  [trace] %s%u\n", label, static_cast<unsigned>(v));
    say(l);
}
u32 fm_callback(u32 x) { return x + 1000; }
u32 fm_var = 123;
struct FmInfo { u32 id, flags; };
FmInfo fm_info = {0x00F1, 0x0007};
unsigned AA = 1000, BB = 2000;
unsigned HOST_K = 1000;
unsigned host_f(unsigned x) { return x * 2 + 1; }
char* s1 = const_cast<char*>("main-s1");
unsigned f(unsigned x) { return x + 10; }
unsigned w(unsigned x) { return x + 20; }
int fixture_printf(const char* fmt, ...) {
    char l[120];
    va_list ap;
    va_start(ap, fmt);
    const int n = std::vsnprintf(l, sizeof l, fmt, ap);
    va_end(ap);
    say("  [printf] ");
    say(l);
    return n;
}
int fixture_puts(const char* s) {
    say("  [printf] ");
    say(s);
    say("\n");
    return 0;
}

/* Not std::malloc: it takes the kernel's critical section, and on this port leaving that
 * turns interrupts on, even under app_lock. A thread could then be preempted holding the
 * lock. */
void* locked_malloc(unsigned n) {
    app_lock();
    void* p = os::estd::pmr::get_default_resource()->allocate(n);
    app_unlock();
    return p;
}
void locked_free(void* p) {
    app_lock();
    if (p) os::estd::pmr::get_default_resource()->deallocate(p, 0);
    app_unlock();
}
void* locked_new(unsigned n) { return locked_malloc(n); }
void locked_delete(void* p) { locked_free(p); }
void locked_delete2(void* p, unsigned) { locked_free(p); }
double fixture_log(double x) { return static_cast<double>(logf(static_cast<float>(x))); }

/* Weak defaults: a firmware without the card still links and still exports the names. */
__attribute__((weak)) int board_sd_store(const void*, u32) { return 0; }
__attribute__((weak)) int board_sd_load(void*, u32) { return 0; }

#if defined(APP_EH)
void* __cxa_allocate_exception(unsigned);
void __cxa_throw(void*, void*, void (*)(void*));
void* __cxa_begin_catch(void*);
void __cxa_end_catch();
void __cxa_end_cleanup();
int __gxx_personality_v0();
void __aeabi_unwind_cpp_pr0();
void __aeabi_unwind_cpp_pr1();
void* __dynamic_cast(const void*, const void*, const void*, int);
void __cxa_pure_virtual();
extern char _ZTVN10__cxxabiv117__class_type_infoE[], _ZTVN10__cxxabiv120__si_class_type_infoE[];
void* locked_new_nothrow(unsigned n, const std::nothrow_t&) { return locked_malloc(n); }
void locked_delete_nothrow(void* p, const std::nothrow_t&) { locked_free(p); }
/* A throw allocates through libsupc++'s malloc, under app_lock: the same problem. The
 * firmware links with --wrap=malloc,--wrap=free, so that goes through the locked heap. */
void* __wrap_malloc(unsigned n) { return locked_malloc(n); }
void __wrap_free(void* p) { locked_free(p); }
#endif
}

__attribute__((weak)) bool board_sd_begin() { return false; }

#define SYM(n, p) {n, reinterpret_cast<u32>(p)}
const scl_sym_entry kMainSymbols[] = {
    SYM("board_sd_store", &board_sd_store), SYM("board_sd_load", &board_sd_load),
    SYM("memcpy", &memcpy), SYM("memset", &memset), SYM("memcmp", &memcmp), SYM("strlen", &strlen),
    SYM("strcat", &strcat),
    SYM("trace_event", &trace_event), SYM("trace_u32", &trace_u32),
    SYM("fm_callback", &fm_callback), SYM("fm_var", &fm_var), SYM("fm_info", &fm_info),
    SYM("sinf", static_cast<float (*)(float)>(&sinf)), SYM("cosf", static_cast<float (*)(float)>(&cosf)),
    SYM("logf", static_cast<float (*)(float)>(&logf)), SYM("sqrtf", static_cast<float (*)(float)>(&sqrtf)),
    SYM("malloc", &locked_malloc), SYM("free", &locked_free), SYM("_Znwj", &locked_new),
    SYM("_ZdlPv", &locked_delete), SYM("_ZdlPvj", &locked_delete2),
    SYM("HOST_K", &HOST_K), SYM("host_f", &host_f),
    SYM("AA", &AA), SYM("BB", &BB), SYM("s1", &s1), SYM("f", &f), SYM("w", &w),
    SYM("printf", &fixture_printf), SYM("puts", &fixture_puts),
    SYM("log", &fixture_log), SYM("__aeabi_d2f", &__aeabi_d2f), SYM("__aeabi_f2d", &__aeabi_f2d),
#if defined(APP_EH)
    SYM("__cxa_allocate_exception", &__cxa_allocate_exception), SYM("__cxa_throw", &__cxa_throw),
    SYM("__cxa_begin_catch", &__cxa_begin_catch), SYM("__cxa_end_catch", &__cxa_end_catch),
    SYM("__cxa_end_cleanup", &__cxa_end_cleanup), SYM("__gxx_personality_v0", &__gxx_personality_v0),
    SYM("__aeabi_unwind_cpp_pr0", &__aeabi_unwind_cpp_pr0), SYM("__aeabi_unwind_cpp_pr1", &__aeabi_unwind_cpp_pr1),
    SYM("__dynamic_cast", &__dynamic_cast), SYM("__cxa_pure_virtual", &__cxa_pure_virtual),
    SYM("_ZTVN10__cxxabiv117__class_type_infoE", &_ZTVN10__cxxabiv117__class_type_infoE),
    SYM("_ZTVN10__cxxabiv120__si_class_type_infoE", &_ZTVN10__cxxabiv120__si_class_type_infoE),
    SYM("_ZSt7nothrow", &std::nothrow), SYM("_ZnwjRKSt9nothrow_t", &locked_new_nothrow),
    SYM("_ZdlPvRKSt9nothrow_t", &locked_delete_nothrow),
#endif
};
const std::size_t kMainSymbolCount = sizeof kMainSymbols / sizeof kMainSymbols[0];
