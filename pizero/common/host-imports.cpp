/**
 * @file host-imports.cpp
 * @brief What the fixtures import from main: the same functions the WeAct firmwares give them,
 * made safe across two cores, and the table the loaders resolve them from.
 */
#include "pizero.hpp"

#include <bsp/uart.hpp>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#if defined(APP_EH)
#include <new>
#endif

using u32 = std::uint32_t;

extern "C" {
unsigned port_cpu_id(void);
int board_flash_read_id(std::uint8_t* mid, std::uint8_t* tid);
void board_flash_erase_sector(std::uint32_t addr);
int board_flash_program(std::uint32_t addr, const std::uint8_t* data, std::uint32_t len);
const std::uint8_t* board_flash_map(std::uint32_t addr);
float __aeabi_d2f(double);
double __aeabi_f2d(float);
}

namespace {
constexpr u32 kAppLock = 0xD0000100u + 30u * 4u;
u32 g_pm[2];
unsigned g_depth[2];
}  // namespace

/* Recursive: a fixture called under the lock may allocate, and malloc takes it too. */
void app_lock() {
    u32 pm;
    __asm__ volatile("mrs %0, primask\n cpsid i" : "=r"(pm) : : "memory");
    const unsigned me = port_cpu_id() & 1u;
    if (g_depth[me]++ == 0) {
        while (*reinterpret_cast<volatile u32*>(kAppLock) == 0) {
        }
        __asm__ volatile("dmb" : : : "memory");
        g_pm[me] = pm;
    }
}
void app_unlock() {
    const unsigned me = port_cpu_id() & 1u;
    if (--g_depth[me] == 0) {
        const u32 pm = g_pm[me];
        __asm__ volatile("dmb" : : : "memory");
        *reinterpret_cast<volatile u32*>(kAppLock) = 1;
        __asm__ volatile("msr primask, %0" : : "r"(pm) : "memory");
    }
}

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
void* locked_malloc(unsigned n) { app_lock(); void* p = std::malloc(n); app_unlock(); return p; }
void locked_free(void* p) { app_lock(); std::free(p); app_unlock(); }
void* locked_new(unsigned n) { return locked_malloc(n); }
void locked_delete(void* p) { locked_free(p); }
void locked_delete2(void* p, unsigned) { locked_free(p); }
double fixture_log(double x) { return static_cast<double>(logf(static_cast<float>(x))); }
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
#endif
}

#define SYM(n, p) {n, reinterpret_cast<u32>(p)}
const scl_sym_entry kMainSymbols[] = {
    SYM("board_flash_read_id", &board_flash_read_id), SYM("board_flash_erase_sector", &board_flash_erase_sector),
    SYM("board_flash_program", &board_flash_program), SYM("board_flash_map", &board_flash_map),
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
