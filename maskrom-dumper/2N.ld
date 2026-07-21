ENTRY(main)
MEMORY
{
    RAM (xr) : ORIGIN = 0x08132000 , LENGTH = 64K   /* gme_alloc_binary_region@0x0802fb20 -> DAT_0802fb7c */
    VAR (rw) : ORIGIN = 0x08141000 , LENGTH = 64K
}
SECTIONS
{
    .text : { *(.text) *(.text.*) . = ALIGN(4); } >RAM
    .data : { *(.data) . = ALIGN(4); } >RAM
    .bss  : { *(.bss)  . = ALIGN(4); } >VAR
}
