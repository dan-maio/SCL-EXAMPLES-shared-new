/* ovadd.c — overlay module 1. float_add, placed by the linker in the shared
 * SRAM window (VMA == the window). NON-PIC + -mlong-calls: bound to fixed
 * addresses at LINK time; the overlay manager just memcpy's these bytes into
 * the window before the call. No relocation, no GOT, no constructors. */
#define OVADD __attribute__((section(".ovadd.text"), noinline, used))
OVADD float ov_add(float a, float b) { return a + b; }
