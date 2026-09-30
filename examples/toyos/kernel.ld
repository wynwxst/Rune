/* Where each part of the kernel goes. QEMU's multiboot loader puts the
 * image at the addresses written here, and looks for the multiboot header
 * in its first 8 KB — which is why that header comes first. */
ENTRY(_start)

SECTIONS
{
    . = 1M;

    .text ALIGN(4K) : {
        KEEP(*(.multiboot))
        *(.text .text.*)
    }
    .rodata ALIGN(4K) : { *(.rodata .rodata.*) }
    .data ALIGN(4K) : { *(.data .data.*) }
    .bss ALIGN(4K) : {
        *(COMMON)
        *(.bss .bss.*)
    }

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
