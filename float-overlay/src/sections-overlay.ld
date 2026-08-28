/* sections-overlay.ld — non-PIC STM32F411 layout for the scl::overlay demo.
 * Same simple base as plain.ld, PLUS one OVERLAY block: two modules (ovadd,
 * ovmul) share a single SRAM window (same VMA) and are stored back-to-back in
 * flash (each at its own LMA). "Loading" a module = memcpy LMA -> window + DSB/ISB.
 * NOCROSSREFS makes ld fail the link if the two modules ever reference each other
 * (they can never be co-resident). The window is carved off the top of RAM,
 * below the stack.
 */
ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 120K          /* .data/.bss/stack */
    OVL   (rwx) : ORIGIN = 0x2001E000, LENGTH = 8K            /* the shared overlay window */
}

_estack = ORIGIN(RAM) + LENGTH(RAM);          /* stack top, just below the OVL window */
_ovl_window_size = LENGTH(OVL);

SECTIONS
{
    .isr_vector : ALIGN(4) { KEEP(*(.isr_vector)) } > FLASH

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

    /* ===================== THE OVERLAY ===================== */
    /* Both modules get VMA == ADDR(OVL) (the window); the linker gives each its own
     * flash LMA (AT>FLASH, back-to-back). main copies the right one in before the call. */
    OVERLAY : NOCROSSREFS
    {
        .ovadd { KEEP(*(.ovadd.text)) KEEP(*(.ovadd.rodata)) KEEP(*(.ovadd.data)) }
        .ovmul { KEEP(*(.ovmul.text)) KEEP(*(.ovmul.rodata)) KEEP(*(.ovmul.data)) }
    } > OVL AT> FLASH

    _ovadd_lma = LOADADDR(.ovadd);  _ovadd_size = SIZEOF(.ovadd);
    _ovmul_lma = LOADADDR(.ovmul);  _ovmul_size = SIZEOF(.ovmul);
    _ovl_vma   = ADDR(.ovadd);      /* shared run address == ORIGIN(OVL) */
    /* ======================================================= */

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

    ASSERT(SIZEOF(.ovadd) <= _ovl_window_size, "overlay ovadd larger than the OVL window")
    ASSERT(SIZEOF(.ovmul) <= _ovl_window_size, "overlay ovmul larger than the OVL window")

    /DISCARD/ : { *(.ARM.attributes) *(.comment) }
}
