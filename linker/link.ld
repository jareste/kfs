ENTRY(start)

SECTIONS {
    . = 0x100000;

    .multiboot : {
        *(.multiboot)
    }

    .text : {
        *(.text)
    }

    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }

    .data : {
        *(.data)

        . = ALIGN(4);
        __ksymtab_start = .;
        KEEP(*(__ksymtab))
        __ksymtab_end = .;
    }

    .bss : {
        *(COMMON)
        *(.bss)
    }

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
