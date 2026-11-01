/* arena.S — the DRAM the loaders use, and scl::optimal's fixed windows.
 *
 * The Lyra has no application flash: "XIP" images are provisioned into DRAM and
 * run in place there. The arena below is ordinary .bss, so the kernel's .bss
 * zeroing covers it.
 */
#ifndef SCL_ARENA_BYTES
#define SCL_ARENA_BYTES 98304
#endif

    .section .bss.scl_arena, "aw", %nobits
    .balign 8
    .global _simage_arena_start
_simage_arena_start:
    .space SCL_ARENA_BYTES   /* 4 slots of kArenaBytes */
    .global _simage_arena_end
_simage_arena_end:

/* scl::optimal links its libraries at these addresses: see CMakeLists.txt. */
    .global _sxip_text_start, _sxip_text_end, _sxip_data_start, _sxip_data_end
    .set _sxip_text_start, 0x07A00000
    .set _sxip_text_end,   0x07A08000
    .set _sxip_data_start, 0x07A80000
    .set _sxip_data_end,   0x07A88000

/* Enter a library with r10 = its GOT base: r0 = fn, r1 = got, then up to four arguments.
 * Floats stay in s0, s1. main is ARM and the fixtures are Thumb, so blx switches.
 * An exception unwinds through this frame: it carries unwind info. */
    .syntax unified
    .arm
    .text
    .global call_lib
    .type call_lib, %function
call_lib:
    .fnstart
    push {r4, r5, r10, lr}
    .save {r4, r5, r10, lr}
    mov r10, r1
    mov r12, r0
    mov r0, r2
    mov r1, r3
    ldr r2, [sp, #16]
    ldr r3, [sp, #20]
    blx r12
    pop {r4, r5, r10, pc}
    .fnend
    .size call_lib, . - call_lib
