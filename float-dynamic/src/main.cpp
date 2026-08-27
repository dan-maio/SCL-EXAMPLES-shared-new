/**
 * @file main.cpp — float-dynamic: load floatlib.so with the POSIX dlopen API.
 *
 * Model E (import_in_project_full.md): scl::dynamic-newlib, hosted POSIX
 * dlopen/dlsym/dlclose over the dl:: facade; the module is a plain -fPIC .so
 * copied into a RAM buffer. Simple: open, resolve two exports by name, call,
 * close. WeAct F411CE, semihosting console.
 */
#include <cstdio>
#include "firmware/dl-impl.hpp"

/* the standard POSIX API, backed by the SCL dl:: facade */
extern "C" {
void*       dlopen (const char* p, int)  { return dl::dlopen(p); }
void*       dlsym  (void* h, const char* n) { return dl::dlsym((dl::handle)h, n); }
int         dlclose(void* h)             { return dl::dlclose((dl::handle)h); }
const char* dlerror(void)                { return dl::dlerror(); }
}

#ifndef SCL_SO_DIR
#define SCL_SO_DIR "."
#endif

/* tiny float -> "int.frac" (3 decimals), no _printf_float needed */
static void fstr(float v, char* p) {
    if (v < 0) { *p++ = '-'; v = -v; }
    unsigned ip = (unsigned)v, fp = (unsigned)((v - (float)ip) * 1000.0f + 0.5f);
    char* q = p; int n = 0; unsigned t = ip;
    do { ++n; t /= 10; } while (t);
    q += n; *q-- = 0; t = ip; do { *q-- = char('0' + t % 10); t /= 10; } while (t);
    p += n; *p++ = '.'; *p++ = char('0' + fp / 100 % 10);
    *p++ = char('0' + fp / 10 % 10); *p++ = char('0' + fp % 10); *p = 0;
}

int main(void) {
    puts("float-dynamic: POSIX dlopen of floatlib.so (RAM / scl::dynamic-newlib)");

    void* h = dlopen(SCL_SO_DIR "/floatlib.so", 0);
    if (!h) { printf("  dlopen failed: %s\nRESULT: FAILURE\n", dlerror()); return 1; }

    float (*add)(float, float) = (float (*)(float, float))dlsym(h, "float_add");
    float (*mul)(float, float) = (float (*)(float, float))dlsym(h, "float_mul");
    if (!add || !mul) { printf("  dlsym failed: %s\nRESULT: FAILURE\n", dlerror()); return 1; }

    char sa[16], sm[16];
    fstr(add(2.5f, 3.5f), sa);          /* 6.000 */
    fstr(mul(2.5f, 3.5f), sm);          /* 8.750 */
    printf("  float_add(2.500, 3.500) = %s\n", sa);
    printf("  float_mul(2.500, 3.500) = %s\n", sm);

    dlclose(h);
    printf("RESULT: SUCCESS\n");
    for (;;) { }
}
