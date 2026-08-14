ENTRY(start)

PHDRS {
    text PT_LOAD FLAGS(5);  /* R+X  - multiboot header + code */
    rodata PT_LOAD FLAGS(4);  /* R    - read-only data */
    data PT_LOAD FLAGS(6);  /* R+W  - writable data/bss */
}

SECTIONS {
    . = 0x100000;

    .multiboot : {
        *(.multiboot)
    } :text

    .text : {
        *(.text)
    } :text

    .rodata : {
        *(.rodata)
        *(.rodata.*)
    } :rodata

    .data : {
        *(.data)

        . = ALIGN(4);
        __ksymtab_start = .;
        KEEP(*(__ksymtab))
        __ksymtab_end = .;
    } :data

    .bss : {
        *(COMMON)
        *(.bss)
    } :data

    /*
     * _kernel_end marks the first byte past the kernel image.
     * The PMM places its bitmap here (page-aligned by pmm_init).
     * The dot (.) is already aligned to 4 bytes by default;
     * we align to 4096 (PAGE_SIZE) so the PMM bitmap starts on a
     * clean page boundary.
     */
    . = ALIGN(4096);
    _kernel_end = .;
}
