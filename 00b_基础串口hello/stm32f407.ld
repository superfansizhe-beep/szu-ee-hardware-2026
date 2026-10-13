/* stm32f407.ld —— 链接脚本（和 L1 完全相同）
 * 规定程序各段放在内存的哪里：代码/向量表放 Flash(0x08000000)，
 * 变量/栈放 RAM(0x20000000)，栈顶 _estack = RAM 末尾 0x20020000。 */
ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 1024K
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
  .isr_vector : { KEEP(*(.isr_vector)) } > FLASH
  .text       : { *(.text*) }            > FLASH
  .rodata     : { *(.rodata*) }          > FLASH

  _sidata = LOADADDR(.data);
  .data : {
    _sdata = .;
    *(.data*);
    _edata = .;
  } > RAM AT > FLASH

  .bss : {
    _sbss = .;
    *(.bss*);
    *(COMMON);
    _ebss = .;
  } > RAM

  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.attributes) }
}
