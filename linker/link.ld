/* linker.ld — link your kernel at 1 MiB */
OUTPUT_FORMAT("elf32-i386")
ENTRY(start)

SECTIONS {
  . = 0x00100000;

  .multiboot ALIGN(4): {
    *(.multiboot)
  }

  .text : {
    *(.text)
  }

  .rodata : {
    *(.rodata*)
  }

  .data ALIGN(0x1000): {
    *(.data)
  }

  .bss  ALIGN(0x1000): {
    *(COMMON)
    *(.bss)
  }

  /* optional symbols for end of kernel image */
  endkernel = .;
}
