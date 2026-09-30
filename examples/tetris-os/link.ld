/* The whole disk, as load addresses: the boot sector as sector 0, the kernel
 * from sector 1. The BIOS runs the boot sector at 0x7C00; the boot sector
 * loads the kernel to 0x10000 and jumps to it. tools/run.sh copies each
 * section to its load address, AT(...), to make the disk image. */
ENTRY(_start)

SECTIONS
{
    .boot 0x7C00 : AT(0) { KEEP(*(.boot)) }

    . = 0x10000;
    .text : AT(0x200) {
        KEEP(*(.text.prologue))
        *(.text .text.*)
    }
    .rodata ALIGN(16) : { *(.rodata .rodata.*) }
    .data ALIGN(16) : { *(.data .data.*) }
    .bss ALIGN(16) : {
        *(COMMON)
        *(.bss .bss.*)
    }
    kernel_end = .;
    /* Everything the kernel is has to fit below the VGA memory at 0xA0000. */
    ASSERT(kernel_end < 0x9F000, "the kernel no longer fits below 0xA0000")

    /* The boot sector reads 1280 sectors whatever size the kernel is, so the
     * disk has to have them. */
    .pad : AT(0x200 + 1280 * 512 - 1) { BYTE(0) }

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
