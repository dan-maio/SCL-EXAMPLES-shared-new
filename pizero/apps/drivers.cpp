/**
 * @file drivers.cpp — the RP2350 versions of the WeAct driver libraries: libled blinks the LED,
 * libuart writes to the UART, and libspi moves bytes through the SPI block in internal loopback.
 */
#include "pizero.hpp"
#include <cstdio>

const char* const kAppName = "drivers-" LOADER_NAME;

static Lib g_libs[3];
static Fn led_init, led_blink, led_is_on, uart_init, uart_puts, spi_init, spi_selftest, spi_transfers;

bool app_load() {
    static const char* const set[] = {"libled", "libuart"};
    if (!lib_load_set(g_libs, set, 2) || !lib_load(g_libs[2], 2, "libspi") || !libs_ready()) return false;
    led_init = lib_fn(g_libs[0], "led_init"); led_blink = lib_fn(g_libs[0], "led_blink"); led_is_on = lib_fn(g_libs[0], "led_is_on");
    uart_init = lib_fn(g_libs[1], "uart_init"); uart_puts = lib_fn(g_libs[1], "uart_puts");
    spi_init = lib_fn(g_libs[2], "spi_init"); spi_selftest = lib_fn(g_libs[2], "spi_selftest"); spi_transfers = lib_fn(g_libs[2], "spi_transfers");
    call(led_init);
    call(uart_init);
    call(spi_init);
    return true;
}

bool app_start() { return true; }

void app_on_core(unsigned core) {
    show("led_blink(3) [watch the LED], total blinks", call(led_blink, 3, 400000));
    show("led_is_on()", call(led_is_on), 0u);
    char l[64];
    std::snprintf(l, sizeof l, "  [libuart] hello from core %u, on the UART only\r\n", core);
    const std::uint32_t sent = call(uart_puts, reinterpret_cast<std::uintptr_t>(l));
    show("uart_puts(...), chars sent", sent);
    expect(sent > 0u, "libuart wrote to the UART");
    show("spi_selftest(0x5A, 32) [loopback], mismatches", call(spi_selftest, 0x5A, 32), 0u);
    show("spi_transfers()", call(spi_transfers));
}
