/* stm32f407.ld —— 雷达版链接脚本（与范本一致）
 * RAM 截短到 0x3FFF0：顶端 16 字节留给 twin mailbox（0x2003FFF0..0x2003FFFF），
 * _estack = 0x2003FFF0，栈向下生长，永不踩到 mailbox。 */
ENTRY(Reset_Handler)
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 2048K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 0x3FFF0
}
SECTIONS
{
    .text : {
        KEEP(*(.isr_vector))
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH

    _sidata = LOADADDR(.data);
    .data : {
        . = ALIGN(4);
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH

    .bss (NOLOAD) : {
        . = ALIGN(4);
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.attributes) *(.comment) }

    _estack = ORIGIN(RAM) + LENGTH(RAM);
}
