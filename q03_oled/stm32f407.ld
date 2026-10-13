/* stm32f407.ld —— 标准链接脚本（128K RAM） */
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
  .data : { _sdata = .; *(.data*); _edata = .; } > RAM AT > FLASH
  .bss  : { _sbss = .; *(.bss*); *(COMMON); _ebss = .; } > RAM
  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.attributes) }
}
