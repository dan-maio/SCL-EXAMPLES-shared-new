/* sections-overlay.ld — non-PIC STM32F411 layout for combo-overlay. TWO overlay
 * modules time-share one SRAM window: .ovfloat (float_add/float_mul) and .ovspifs
 * (the whole W25Q64 flat-fs driver). Both bound to the same VMA, stored at their
 * own flash LMA; NOCROSSREFS makes ld fail if they ever reference each other.
 * The OVERLAY block is placed BEFORE .text so its explicit object selectors claim
 * the modules first. "Loading" a module = memcpy its bytes into the window.
 * Object paths are the deterministic CMake names for the `combo-overlay` target.
 */
ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 112K
    OVL   (rwx) : ORIGIN = 0x2001C000, LENGTH = 16K       /* shared window (holds the larger module) */
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
_ovl_window_size = LENGTH(OVL);

SECTIONS
{
    .isr_vector : ALIGN(4) { KEEP(*(.isr_vector)) } > FLASH

    /* ===== TWO overlay modules, ONE window, claimed FIRST ===== */
    OVERLAY : NOCROSSREFS
    {
        .ovfloat
        {
            KEEP(CMakeFiles/combo-overlay.dir/fixtures/src/ovfloat.cpp.obj(.text .text.* .rodata .rodata.* .data .data.*))
        }
        .ovspifs
        {
            KEEP(CMakeFiles/combo-overlay.dir/fixtures/src/w25q64.cpp.obj(.text .text.* .rodata .rodata.* .data .data.*))
            KEEP(CMakeFiles/combo-overlay.dir/fixtures/src/spi-shim.cpp.obj(.text .text.* .rodata .rodata.* .data .data.*))
            KEEP(CMakeFiles/combo-overlay.dir/fixtures/src/fs-archive-spi.cpp.obj(.text .text.* .rodata .rodata.* .data .data.*))
            KEEP(CMakeFiles/combo-overlay.dir/fixtures/src/spifs-api.cpp.obj(.text .text.* .rodata .rodata.* .data .data.*))
        }
    } > OVL AT> FLASH
    _ovfloat_lma = LOADADDR(.ovfloat);  _ovfloat_size = SIZEOF(.ovfloat);
    _ovspifs_lma = LOADADDR(.ovspifs);  _ovspifs_size = SIZEOF(.ovspifs);
    _ovl_vma     = ADDR(.ovfloat);      /* shared run address (== ADDR(.ovspifs)) */
    /* ========================================================= */

    .text : ALIGN(4)
    {
        KEEP(*(.text .text.*))
        KEEP(*(.rodata .rodata.* .constdata .constdata.*))
        *(.glue_7) *(.glue_7t)
        KEEP(*(.eh_frame*))
        . = ALIGN(4);
    } > FLASH

    .ARM.exidx : ALIGN(4) { *(.ARM.exidx* .gnu.linkonce.armexidx.*) } > FLASH

    .init_array : ALIGN(4)
    {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > FLASH

    . = ALIGN(4);
    _sidata = LOADADDR(.data);
    .data : ALIGN(4)
    {
        _sdata = .;
        KEEP(*(.data .data.*))
        . = ALIGN(4);
        _edata = .;
    } > RAM AT> FLASH

    .bss (NOLOAD) : ALIGN(4)
    {
        _sbss = .;
        KEEP(*(.bss .bss.*))
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    . = ALIGN(8);
    PROVIDE(end = .);

    ASSERT(SIZEOF(.ovfloat) <= _ovl_window_size, "ovfloat larger than the OVL window")
    ASSERT(SIZEOF(.ovspifs) <= _ovl_window_size, "spifs driver larger than the OVL window")

    /DISCARD/ : { *(.ARM.attributes) *(.comment) }
}
