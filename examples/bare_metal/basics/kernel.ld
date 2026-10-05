/*
 * Rune x86-64 kernel linker script
 *
 * The kernel is loaded at physical address 1 MiB.
 *
 * The Multiboot 1 header must be located within the first 8192
 * bytes of the kernel image, so .multiboot is placed first.
 */

ENTRY(_start)

SECTIONS
{
    /*
     * ------------------------------------------------------------
     * Kernel load address
     * ------------------------------------------------------------
     */

    . = 1M;


    /*
     * ------------------------------------------------------------
     * Multiboot header + executable code
     * ------------------------------------------------------------
     *
     * .multiboot MUST remain near the beginning of the image.
     */

    .text ALIGN(4K) :
    {
        KEEP(*(.multiboot))

        *(.text)
        *(.text.*)
    }


    /*
     * ------------------------------------------------------------
     * Read-only data
     * ------------------------------------------------------------
     */

    .rodata ALIGN(4K) :
    {
        *(.rodata)
        *(.rodata.*)
    }


    /*
     * ------------------------------------------------------------
     * Initialized writable data
     * ------------------------------------------------------------
     */

    .data ALIGN(4K) :
    {
        *(.data)
        *(.data.*)
    }


    /*
     * ------------------------------------------------------------
     * Uninitialized data
     *
     * This contains:
     *
     *   boot_magic
     *   boot_info
     *   stack
     *   pml4
     *   pdpt
     *   page_directory
     *
     * from boot.s.
     *
     * The loader/runtime will zero this region.
     * ------------------------------------------------------------
     */

    .bss ALIGN(4K) :
    {
        __bss_start = .;

        *(COMMON)
        *(.bss)
        *(.bss.*)

        __bss_end = .;
    }


    /*
     * ------------------------------------------------------------
     * Useful kernel boundaries
     * ------------------------------------------------------------
     */

    . = ALIGN(4K);

    __kernel_end = .;


    /*
     * ------------------------------------------------------------
     * Discard compiler/linker metadata
     * ------------------------------------------------------------
     */

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
