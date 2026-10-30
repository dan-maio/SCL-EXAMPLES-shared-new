/* arena.S — the RAM the loaders use, and scl::optimal's fixed windows. */
    .section .bss.scl_arena, "aw", %nobits
    .balign 8
    .global _simage_arena_start
_simage_arena_start:
    .space 65536
    .global _simage_arena_end
_simage_arena_end:

/* floatlib.opt.so is linked at these addresses: see CMakeLists.txt. */
    .global _sxip_text_start, _sxip_text_end, _sxip_data_start, _sxip_data_end
    .set _sxip_text_start, 0x20070000
    .set _sxip_text_end,   0x20078000
    .set _sxip_data_start, 0x20078000
    .set _sxip_data_end,   0x20080000
