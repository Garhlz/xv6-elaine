ENTRY(_start)

SECTIONS
{
  . = 0;

  .text : {
    *(.text .text.*)
  }

  .rodata : {
    *(.srodata .srodata.*)
    *(.rodata .rodata.*)
  }

  . = ALIGN(16);

  .data : {
    *(.sdata .sdata.*)
    *(.data .data.*)
  }

  . = ALIGN(16);

  .bss : {
    *(.sbss .sbss.*)
    *(.bss .bss.*)
    *(COMMON)
  }

  . = ALIGN(16);
  PROVIDE(end = .);
}
