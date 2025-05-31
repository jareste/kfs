; entry.asm — ELF32/i386 multiboot kernel entry
; nasm -f elf32 entry.asm -o entry.o

BITS 32
extern kernel_main
global start

section .multiboot
  align 4
  dd 0x1BADB002            ; Multiboot header magic
  dd 0x00000003            ; flags: align modules & mmap
  dd -(0x1BADB002 + 0x00000003)

section .text
start:
  cli                      ; disable interrupts
  ; GRUB has already enabled protected mode and loaded us at 1MiB
  ; EAX = MULTIBOOT_BOOTLOADER_MAGIC
  ; EBX = pointer to multiboot_info_t

  mov  ebp, esp            ; keep old stack frame if you like
  mov  esp, KSTACK_TOP     ; set up your kernel stack

  ; push args in reverse order (cdecl):
  push ebx                 ; arg2 = mbi_addr
  push eax                 ; arg1 = magic
  call kernel_main         ; kernel_main(magic, mbi_addr)

.hang:
  hlt
  jmp .hang

section .bss
  align 16
KSTACK_BOTTOM:
  resb 0x2000              ; 8 KiB stack
KSTACK_TOP:
