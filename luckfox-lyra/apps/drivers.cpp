/**
 * @file drivers.cpp — the RK3506 versions of the WeAct driver libraries: libled blinks the
 * board's user LED, libuart writes to the debug UART, and libsd moves bytes through the SD
 * card (the board has no application SPI flash).
 */
#include "lyra.hpp"
#include <cstdio>

const char* const kAppName = "drivers-" LOADER_NAME;

static Lib g_libs[3];
static Fn led_init, led_blink, led_is_on, uart_init, uart_puts, uart_chars, sd_init, sd_selftest, sd_transfers;

bool app_load() {
    static const char* const set[] = {"libled", "libuart"};
    if (!lib_load_set(g_libs, set, 2) || !lib_load(g_libs[2], 2, "libsd") || !libs_ready()) return false;
    led_init = lib_fn(g_libs[0], "led_init"); led_blink = lib_fn(g_libs[0], "led_blink"); led_is_on = lib_fn(g_libs[0], "led_is_on");
    uart_init = lib_fn(g_libs[1], "uart_init"); uart_puts = lib_fn(g_libs[1], "uart_puts"); uart_chars = lib_fn(g_libs[1], "uart_chars");
    sd_init = lib_fn(g_libs[2], "sd_init"); sd_selftest = lib_fn(g_libs[2], "sd_selftest"); sd_transfers = lib_fn(g_libs[2], "sd_transfers");
    call(led_init);
    call(uart_init);
    call(sd_init);
    return true;
}

bool app_start() { return board_sd_begin(); }

void app_on_core(unsigned core) {
    say("  blinking the LED (GPIO1_A0) 5 times: watch it\n");
    show("led_blink(5), total blinks", call(led_blink, 5, 4000000));
    show("led_is_on()", call(led_is_on), 0u);
    char l[64];
    std::snprintf(l, sizeof l, "  [libuart] hello from core %u, on the UART only\r\n", core);
    const std::uint32_t sent = call(uart_puts, reinterpret_cast<std::uintptr_t>(l));
    show("uart_puts(...), chars sent", sent);
    expect(sent > 0u, "libuart wrote to the UART");
    show("sd_selftest(0x5A, 32) [write, read back on the SD card], mismatches", call(sd_selftest, 0x5A, 32), 0u);
    show("sd_transfers()", call(sd_transfers));
    show("uart_chars()", call(uart_chars));
}
