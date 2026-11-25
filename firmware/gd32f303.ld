ENTRY(Reset_Handler)
MEMORY {
    FLASH (rx)  : ORIGIN = 0x08008000, LENGTH = 252K
    SETTINGS (r) : ORIGIN = 0x08047000, LENGTH = 4K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
}
_stack_top = ORIGIN(RAM) + LENGTH(RAM) - 8;
_settings_start = ORIGIN(SETTINGS);

SECTIONS {
    .vectors : { KEEP(*(.vectors)) } > FLASH
    .text : { *(.text*) *(.rodata*) . = ALIGN(4); } > FLASH
    .data : {
        _data_start = .;
        *(.data*)
        . = ALIGN(4);
        _data_end = .;
    } > RAM AT> FLASH
    _data_load = LOADADDR(.data);
    .bss (NOLOAD) : {
        _bss_start = .;
        *(.bss*) *(COMMON)
        . = ALIGN(4);
        _bss_end = .;
    } > RAM
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.eh_frame*) }
}
ASSERT(_bss_end + 4096 <= _stack_top, "Insufficient stack space")
ASSERT(ORIGIN(FLASH) + LENGTH(FLASH) == ORIGIN(SETTINGS), "Unexpected journal location")

