ENTRY(main)
MEMORY
{
    RAM (xr) : ORIGIN = 0x080ea000 , LENGTH = 64K   /* FUN_08023324 -> DAT_08023370 */
    VAR (rw) : ORIGIN = 0x080fa000 , LENGTH = 64K
}
SECTIONS
{
    .text : { *(.text) *(.text.*) . = ALIGN(4); } >RAM
    .data : { *(.data) . = ALIGN(4); } >RAM
    .bss  : { *(.bss)  . = ALIGN(4); } >VAR
}
