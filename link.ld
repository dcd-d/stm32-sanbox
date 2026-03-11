MEMORY {
  FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 64K
  RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 20K
}

SECTIONS {
  .text : {
    KEEP(*(.isr_vector))
    *(.text*)
    *(.rodata*)
    *(.ARM.exidx*) /* 收容偷偷生成的孤儿段 */
    . = ALIGN(4);
    _etext = .;
  } > FLASH

  /* 【核心魔法】：直接向链接器索要 .data 在 Flash 里的绝对装载地址 (Load Address) */
  _sidata = LOADADDR(.data);

  .data : {
    _sdata = .;
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } > RAM AT > FLASH

  .bss : {
    _sbss = .;
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    
    /* 提供给 C 标准库底层的堆栈分配器 (malloc/sbrk) 使用的符号 */
    end = .; 
    __end__ = .; 
  } > RAM

  _estack = ORIGIN(RAM) + LENGTH(RAM); /* 栈顶设在 RAM 末尾 */
}