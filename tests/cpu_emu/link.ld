/* (Copied from jhortale/smk37-analog, spike/device-link, spike/cpu/, by João Hortale.) Boot code in XIP at the emulator's load base; everything else runs from RAM. */
ENTRY(_start)
MEMORY
{
    XIP (rx)  : ORIGIN = 0x02000000, LENGTH = 0x40000
    RAM (rwx) : ORIGIN = 0x01C08000, LENGTH = 0x40000
}
SECTIONS
{
    .boot : { KEEP(*(.text.crt0)) *(.text.boot) } > XIP
    .data : {
        . = ALIGN(4); __data_start = .;
        *(.volatile_ram_code*) *(.text*) *(.rodata*) *(.volatile_ram*) *(.data*) *(.sdata*)
        . = ALIGN(4); __data_end = .;
    } > RAM AT > XIP
    __data_lma = LOADADDR(.data);
    .bss (NOLOAD) : { . = ALIGN(4); __bss_start = .; *(.bss*) *(.sbss*) *(COMMON) . = ALIGN(4); __bss_end = .; } > RAM
    /DISCARD/ : { *(.comment) *(.note*) }
}
