ENTRY(reset)

MEMORY {
  ROM   (rx) : ORIGIN = 0x00400000, LENGTH = 0x00020000
  RAM   (rw) : ORIGIN = 0x00001000, LENGTH = 0x003df000
  STACK (rw) : ORIGIN = 0x003e0000, LENGTH = 0x00010000
}

SECTIONS {
  .vectors : { *(.vectors) }                   >ROM
  .text    : { *(.text.boot) *(.text .text.*) } >ROM
  .rodata  : { *(.rodata .rodata.*) }           >ROM
  .data    : {
    __data_start = .;
    *(.data .data.*)
    __data_end = .;
  } >RAM AT>ROM
  __data_load = LOADADDR(.data);
  .bss (NOLOAD) : {
    __bss_start = .;
    *(.bss .bss.*) *(COMMON)
    __bss_end = .;
  } >RAM

  __stack_top = ORIGIN(STACK) + LENGTH(STACK);
  __reset_ssp = __stack_top + 0x00400000;
  __mailbox = 0x00000400;
}
