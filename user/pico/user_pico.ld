ENTRY(_start)

PHDRS
{
  text PT_LOAD FLAGS(5);
  data PT_LOAD FLAGS(6);
}

SECTIONS
{
  . = 0;

  .text : {
    *(.text .text.*)
  } :text

  .rodata : {
    *(.srodata .srodata.*)
    *(.rodata .rodata.*)
    *(.eh_frame .eh_frame.*)
    *(.gcc_except_table .gcc_except_table.*)
  } :text

  /*
   * exec() currently requires every LOAD segment virtual address to be
   * page-aligned, so keep the writable segment on its own page boundary.
   */
  . = ALIGN(4096);

  .data : {
    *(.sdata .sdata.*)
    *(.data .data.*)
  } :data

  . = ALIGN(16);

  .preinit_array : {
    __preinit_array_start = .;
    KEEP(*(.preinit_array .preinit_array.*))
    __preinit_array_end = .;
  } :data

  .init_array : {
    __init_array_start = .;
    KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
    KEEP(*(.init_array))
    __init_array_end = .;
  } :data

  .fini_array : {
    __fini_array_start = .;
    KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
    KEEP(*(.fini_array))
    __fini_array_end = .;
  } :data

  . = ALIGN(16);

  .bss : {
    *(.sbss .sbss.*)
    *(.bss .bss.*)
    *(COMMON)
  } :data

  . = ALIGN(16);
  PROVIDE(end = .);
}
