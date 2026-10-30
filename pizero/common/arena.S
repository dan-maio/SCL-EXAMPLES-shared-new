/* arena.S — the RAM the loaders use, and scl::optimal's fixed windows. */
    .section .bss.scl_arena, "aw", %nobits
    .balign 8
    .global _simage_arena_start
_simage_arena_start:
    .space 98304   /* 4 slots of kArenaBytes */
    .global _simage_arena_end
_simage_arena_end:

/* The optimal libraries are linked at these addresses: see CMakeLists.txt. */
    .global _sxip_text_start, _sxip_text_end, _sxip_data_start, _sxip_data_end
    .set _sxip_text_start, 0x20070000
    .set _sxip_text_end,   0x20078000
    .set _sxip_data_start, 0x20078000
    .set _sxip_data_end,   0x20080000

/* Enter a library with r10 = its GOT base: r0 = fn, r1 = got, then up to four arguments.
 * Floats stay in s0, s1. Exceptions unwind through this frame. */
    .syntax unified
    .thumb
    .text
    .global call_lib
    .type call_lib, %function
    .thumb_func
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
