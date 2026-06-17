/*
 * Picolibc 实验链路的专用 linker script。
 *
 * 设计要点：
 *   - ENTRY(_start) 指定 ELF 入口，exec() 将其写入 epc
 *   - 分两个 LOAD segment：text（只读可执行，含 .text/.rodata）和 data（可读写）
 *   - xv6 的 exec() 要求每个 LOAD segment 的 vaddr 页对齐，
 *     因此在 .data 之前显式 ALIGN(4096)
 *   - .preinit_array / .init_array / .fini_array 用于 constructor/destructor
 *   - PROVIDE(end = .) 给 sbrk 提供堆起始标记
 */

ENTRY(_start)

PHDRS
{
  text PT_LOAD FLAGS(5);   /* PF_R=4 | PF_X=1 = 5（可读可执行） */
  data PT_LOAD FLAGS(6);   /* PF_R=4 | PF_W=2 = 6（可读可写） */
}

SECTIONS
{
  /* 从地址 0 开始（xv6 用户程序不使用虚拟地址偏移） */
  . = 0;

  /* ---- 只读 segment ---- */
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
   * exec() 要求每个 LOAD segment 的虚拟地址页对齐，
   * 因此在可写 segment 开始前跳到一个新的页面边界。
   */
  . = ALIGN(4096);

  /* ---- 可读写 segment ---- */

  .data : {
    *(.sdata .sdata.*)
    *(.data .data.*)
  } :data

  . = ALIGN(16);

  /*
   * constructor/destructor 函数指针数组。
   * KEEP 确保即使未被直接引用，链接器也不丢弃这些 section。
   * SORT_BY_INIT_PRIORITY 按优先级排序（支持 __attribute__((constructor(101)))）。
   */
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

  /* BSS: 未初始化的全局变量，exec 加载时由零填充页提供初始 0 值 */
  .bss : {
    *(.sbss .sbss.*)
    *(.bss .bss.*)
    *(COMMON)
  } :data

  . = ALIGN(16);
  PROVIDE(end = .);  /* 堆的起始地址，sbrk 以此为基准 */
}
