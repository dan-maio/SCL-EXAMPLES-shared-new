/**
 * @file flashsmp.cpp — the spifs library, written to with both cores running. Core 0 formats
 * while core 1 runs code from flash, then both cores create files at once. Each write parks the
 * other core in SRAM, so it keeps its state and carries on: see "writing flash" in pizero.cpp.
 *
 * The filesystem has no lock of its own, so a spinlock with interrupts on serialises calls into
 * it: this core can still be parked while it waits.
 */
#include "spifs-lib.hpp"

#include <cmsis-plus/rtos/os.h>

#include <cstdio>
#include <cstring>

const char* const kAppName = "flashsmp-" LOADER_NAME;

static Spifs g_fs;
static void fs_lock() {
    auto* const lock = reinterpret_cast<volatile std::uint32_t*>(0xD0000100u + 27u * 4u);
    while (*lock == 0) os::rtos::this_thread::yield();
    __asm__ volatile("dmb" : : : "memory");
}
static void fs_unlock() {
    __asm__ volatile("dmb" : : : "memory");
    *reinterpret_cast<volatile std::uint32_t*>(0xD0000100u + 27u * 4u) = 1;
}
static constexpr unsigned kFiles = 12;
static volatile bool g_formatted;
static volatile unsigned g_work[2], g_work_during_format, g_finished[2];

/* Code and data in flash, which a core running while XIP is off would fault on. */
static const std::uint32_t kTable[32] = {3, 1, 4, 1, 5, 9, 2, 6, 5, 3, 5, 8, 9, 7, 9, 3, 2, 3, 8, 4, 6, 2, 6, 4, 3, 3, 8, 3, 2, 7, 9, 5};
__attribute__((noinline)) static std::uint32_t flash_work() {
    std::uint32_t sum = 0;
    for (unsigned r = 0; r < 200; ++r)
        for (const std::uint32_t v : kTable) sum += v;
    return sum;
}

bool app_writes_flash_under_smp() { return true; }

bool app_load() { return g_fs.load(0) && libs_ready(); }

bool app_start() {
    if (g_fs.init() != 0) { say("  flash NOT found\n"); return false; }
    return true;
}

void app_on_core(unsigned) { say("  waiting for the other core, then writing together\n"); }

void app_together(unsigned core) {
    const std::uint32_t want = flash_work();
    if (core == 0) {
        fs_lock();
        say("  core 0: formatting, with core 1 running\n");
        const int r = g_fs.format();
        fs_unlock();
        expect(r == 0, "format with both cores running");
        g_formatted = true;
    } else {
        while (!g_formatted) {
            expect(flash_work() == want, "core 1 kept computing during the format");
            g_work_during_format = g_work_during_format + 1u;
            os::rtos::this_thread::yield();
        }
    }

    for (unsigned i = 0; i < kFiles; ++i) {
        char name[16], body[32], back[32];
        std::snprintf(name, sizeof name, "c%u-%u.txt", core, i);
        const int len = std::snprintf(body, sizeof body, "core %u file %u\n", core, i);
        unsigned got = 0;
        fs_lock();
        const int made = g_fs.create(name, body, static_cast<unsigned>(len));
        const int read = g_fs.read(name, back, sizeof back, &got);
        say("  wrote "); say(name); say("\n");
        fs_unlock();
        expect(made == 0 && read == 0 && got == static_cast<unsigned>(len) && std::memcmp(back, body, got) == 0,
               "a file written with both cores running reads back");
        for (unsigned k = 0; k < 20; ++k) {
            expect(flash_work() == want, "this core kept computing between writes");
            g_work[core] = g_work[core] + 1u;
        }
    }
    g_finished[core] = 1;
    while (!g_finished[1u - core]) os::rtos::sysclock.sleep_for(5);

    if (core == 0) {
        sayf("  files=%u; core 1 worked %u times during the format\n", g_fs.count(), g_work_during_format);
        sayf("  parked for a write: core 0 %u times, core 1 %u times\n", flash_parks(0), flash_parks(1));
        expect(g_fs.count() == 2u * kFiles, "every file is listed");
        expect(g_work_during_format > 0u, "core 1 ran during the format");
        expect(flash_parks(0) > 0u && flash_parks(1) > 0u, "each core was parked for the other's writes");
        g_fs.cat("c1-3.txt", "core 1 file 3\n");
        g_fs.cat("c0-3.txt", "core 0 file 3\n");
    }
}
