/* STM32F407 最小链接脚本（L1 点灯用）
 *
 * 它只回答一个问题：代码和变量分别放到芯片的哪个地址。
 *   Flash 0x08000000，1024K：放向量表和代码（断电不丢）
 *   RAM   0x20000000，128K ：放变量和栈（断电清空），栈顶在 RAM 最上端
 */

ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 1024K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}

SECTIONS
{
    .text :
    {
        KEEP(*(.isr_vector))   /* 向量表必须在 Flash 最开头 */
        *(.text*)              /* 代码 */
        *(.rodata*)            /* 只读常量 */
        . = ALIGN(4);
    } > FLASH

    _sidata = LOADADDR(.data);

    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH         /* 运行在 RAM，但初始值存在 Flash */

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.attributes) *(.comment) }

    _estack = ORIGIN(RAM) + LENGTH(RAM);   /* 栈顶 = 0x20020000 */
}
