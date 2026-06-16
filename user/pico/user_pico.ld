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

  .preinit_array : {
    PROVIDE(__preinit_array_start = .);
    KEEP(*(.preinit_array))
    PROVIDE(__preinit_array_end = .);
  }

  .init_array : {
    PROVIDE(__init_array_start = .);
    KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
    KEEP(*(.init_array))
    PROVIDE(__init_array_end = .);
  }

  .fini_array : {
    PROVIDE(__fini_array_start = .);
    KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
    KEEP(*(.fini_array))
    PROVIDE(__fini_array_end = .);
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
