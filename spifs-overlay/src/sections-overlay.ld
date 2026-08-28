/* sections-overlay.ld — non-PIC STM32F411 layout for spifs-overlay. The whole
 * W25Q64 SPI-flatfs driver (4 objects) is ONE overlay module, bound to a shared
 * SRAM window (VMA) and stored in flash (LMA). "Loading" = memcpy into the window.
 *
 * The OVERLAY block is placed BEFORE .text: its explicit per-object selectors
 * claim the driver's code/rodata/data first (an input section goes to the FIRST
 * matching output section), so the general .text sweep afterwards gets everything
 * else. No EXCLUDE_FILE needed. Object paths are the deterministic CMake names for
 * the `spifs-overlay` target.
 */
ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 112K
    OVL   (rwx) : ORIGIN = 0x2001C000, LENGTH = 16K       /* the overlay window */
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
_ovl_window_size = LENGTH(OVL);

SECTIONS
{
    .isr_vector : ALIGN(4) { KEEP(*(.isr_vector)) } > FLASH

    /* ===== THE OVERLAY (one module = the whole driver), claimed FIRST ===== */
    OVERLAY : NOCROSSREFS
    {
        .ovspifs
        {
            KEEP(CMakeFiles/spifs-overlay.dir/fixtures/src/w25q64.cpp.obj(.text .text.* .rodata .rodata.* .data .data.*))
            KEEP(CMakeFiles/spifs-overlay.dir/fixtures/src/spi-shim.cpp.obj(.text .text.* .rodata .rodata.* .data .data.*))
            KEEP(CMakeFiles/spifs-overlay.dir/fixtures/src/fs-archive-spi.cpp.obj(.text .text.* .rodata .rodata.* .data .data.*))
            KEEP(CMakeFiles/spifs-overlay.dir/fixtures/src/spifs-api.cpp.obj(.text .text.* .rodata .rodata.* .data .data.*))
        }
    } > OVL AT> FLASH
    _ovspifs_lma = LOADADDR(.ovspifs);  _ovspifs_size = SIZEOF(.ovspifs);
    _ovl_vma     = ADDR(.ovspifs);
    /* ===================================================================== */

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

    ASSERT(SIZEOF(.ovspifs) <= _ovl_window_size, "spifs driver larger than the 16K OVL window")

    /DISCARD/ : { *(.ARM.attributes) *(.comment) }
}
