/* reloc.ld — plain, non-PIC STM32F411 layout for the scl::reloc app. An ordinary
 * ET_EXEC: no GOT/PLT/.dynsym/_DYNAMIC. A plain static executable has no runtime
 * symbol table (the .symtab is not in any PT_LOAD), so main maps the module's
 * imports through a small data-driven export table (see main.cpp) — the -pie
 * loaders (static/optimal) instead resolve generically against their .dynsym.
 */
ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);

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
    _sidata = LOADADDR(.data);           /* FLASH master copy of .data */

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
    PROVIDE(end = .);                     /* not used (crt heap is a static array) */

    /DISCARD/ : { *(.ARM.attributes) *(.comment) }
}
