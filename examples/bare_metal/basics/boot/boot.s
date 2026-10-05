/*
 * Rune x86-64 kernel bootstrap
 *
 * GRUB / Multiboot 1 enters us in 32-bit protected mode:
 *
 *   EAX = Multiboot magic
 *   EBX = Multiboot information structure
 *
 * This file:
 *
 *   1. Provides the Multiboot header
 *   2. Saves the Multiboot arguments
 *   3. Creates identity-mapped 64-bit page tables
 *   4. Enables PAE
 *   5. Enables EFER.LME
 *   6. Enables paging
 *   7. Jumps into x86-64 long mode
 *   8. Creates a 64 KB stack
 *   9. Calls Rune's kernel_main(magic, multiboot_info)
 *
 * Everything after start64 executes in 64-bit long mode.
 */


/* ================================================================
 * Multiboot header
 * ================================================================ */

    .set MAGIC,    0x1BADB002
    .set FLAGS,    (1 << 0) | (1 << 1)
    .set CHECKSUM, -(MAGIC + FLAGS)

    .section .multiboot, "a"
    .align 4

    .long MAGIC
    .long FLAGS
    .long CHECKSUM


/* ================================================================
 * Data
 * ================================================================ */

    .section .bss
    .align 16

/*
 * Multiboot arguments.

 * We cannot rely on a stack during the 32-bit bootstrap, so save
 * EAX/EBX here before doing anything that might overwrite them.
 */

boot_magic:
    .long 0

boot_info:
    .long 0


/*
 * 64 KB kernel stack.
 *
 * The stack grows downward from stack_top.
 */

    .align 16

stack_bottom:
    .skip 65536
stack_top:


/*
 * Page tables.
 *
 * We identity-map the first 1 GiB:
 *
 *     virtual 0x00000000 -> physical 0x00000000
 *     virtual 0x00000000 -> physical 0x00000000
 *
 * using 512 × 2 MiB pages.
 *
 * Layout:
 *
 *     PML4
 *       |
 *       v
 *     PDPT
 *       |
 *       v
 *     Page Directory
 *       |
 *       +--> 2 MiB page
 *       +--> 2 MiB page
 *       +--> ...
 *       +--> 2 MiB page
 *
 * One page directory contains 512 × 2 MiB = 1 GiB.
 */

    .align 4096

pml4:
    .skip 4096

    .align 4096

pdpt:
    .skip 4096

    .align 4096

page_directory:
    .skip 4096


/* ================================================================
 * GDT
 * ================================================================ */

    .section .rodata
    .align 8

gdt:

/*
 * GDT entry 0:
 * Null descriptor
 */
gdt_null:
    .quad 0x0000000000000000


/*
 * GDT entry 1:
 *
 * 64-bit kernel code segment
 *
 * Selector = 0x08
 *
 * Descriptor:
 *
 *   Present
 *   Ring 0
 *   Code
 *   Executable
 *   Readable
 *   Long-mode
 */
gdt_code:
    .quad 0x00AF9A000000FFFF


/*
 * GDT entry 2:
 *
 * 64-bit kernel data segment
 *
 * Selector = 0x10
 */
gdt_data:
    .quad 0x00CF92000000FFFF

gdt_end:


gdt_descriptor:
    .word gdt_end - gdt - 1
    .long gdt


/* ================================================================
 * 32-bit bootstrap
 * ================================================================ */

    .section .text
    .global _start
    .type _start, @function

    .code32

_start:

    /*
     * ------------------------------------------------------------
     * Save Multiboot arguments
     * ------------------------------------------------------------
     *
     * At entry:
     *
     *   EAX = Multiboot magic
     *   EBX = Multiboot information structure
     */

    movl %eax, boot_magic
    movl %ebx, boot_info


    /*
     * ------------------------------------------------------------
     * Load our GDT
     * ------------------------------------------------------------
     */

    lgdt gdt_descriptor


    /*
     * ------------------------------------------------------------
     * Build PML4
     * ------------------------------------------------------------
     *
     * PML4[0] -> PDPT
     *
     * Address of pdpt is OR'd with:
     *
     *   0x001 = Present
     *   0x002 = Writable
     */

    movl $pdpt, %eax
    orl  $0x003, %eax
    movl %eax, pml4


    /*
     * ------------------------------------------------------------
     * Build PDPT
     * ------------------------------------------------------------
     *
     * PDPT[0] -> Page Directory
     */

    movl $page_directory, %eax
    orl  $0x003, %eax
    movl %eax, pdpt


    /*
     * ------------------------------------------------------------
     * Build the page directory
     * ------------------------------------------------------------
     *
     * Each entry maps one 2 MiB page.
     *
     * Entry flags:
     *
     *   bit 0 = Present
     *   bit 1 = Writable
     *   bit 7 = Page Size (2 MiB)
     *
     * So:
     *
     *   entry 0 = physical 0x00000000
     *   entry 1 = physical 0x00200000
     *   entry 2 = physical 0x00400000
     *   ...
     *   entry 511 = physical 0x3FE00000
     *
     * This identity maps the first 1 GiB.
     */

    movl $page_directory, %edi
    xorl %ecx, %ecx

.Lfill_page_directory:

    /*
     * Physical address:
     *
     *   ECX * 2 MiB
     *
     * Since ECX <= 511, this fits inside 32 bits.
     */

    movl %ecx, %eax
    shll $21, %eax

    /*
     * Present + writable + 2 MiB page
     */

    orl $0x083, %eax

    movl %eax, (%edi, %ecx, 8)

    /*
     * Clear upper 32 bits of each 64-bit entry.
     */

    movl $0, 4(%edi, %ecx, 8)

    incl %ecx
    cmpl $512, %ecx
    jne .Lfill_page_directory


    /*
     * ------------------------------------------------------------
     * Load PML4 into CR3
     * ------------------------------------------------------------
     */

    movl $pml4, %eax
    movl %eax, %cr3


    /*
     * ------------------------------------------------------------
     * Enable PAE
     * ------------------------------------------------------------
     *
     * CR4.PAE = bit 5
     */

    movl %cr4, %eax
    orl  $0x20, %eax
    movl %eax, %cr4


    /*
     * ------------------------------------------------------------
     * Enable long mode
     * ------------------------------------------------------------
     *
     * IA32_EFER MSR = 0xC0000080
     *
     * EFER.LME = bit 8
     */

    movl $0xC0000080, %ecx

    rdmsr

    orl $0x100, %eax

    wrmsr


    /*
     * ------------------------------------------------------------
     * Enable paging
     * ------------------------------------------------------------
     *
     * CR0.PG = bit 31
     *
     * CR0.PE should already be enabled because Multiboot entered
     * us in protected mode.
     */

    movl %cr0, %eax
    orl  $0x80000000, %eax
    movl %eax, %cr0


    /*
     * ------------------------------------------------------------
     * Enter 64-bit long mode
     * ------------------------------------------------------------
     *
     * 0x08 = GDT code segment.
     *
     * The far jump causes the CPU to load the 64-bit code segment
     * and begin executing in long mode.
     */

    ljmp $0x08, $start64


/* ================================================================
 * 64-bit kernel entry
 * ================================================================ */

    .code64

start64:

    /*
     * ------------------------------------------------------------
     * Load data segments
     * ------------------------------------------------------------
     *
     * 0x10 = GDT data segment.
     *
     * In long mode these segment bases/limits are mostly ignored,
     * but loading valid selectors is still useful.
     */

    movw $0x10, %ax

    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss


    /*
     * FS/GS are normally left at zero unless the kernel needs them.
     */

    xorw %ax, %ax

    movw %ax, %fs
    movw %ax, %gs


    /*
     * ------------------------------------------------------------
     * Establish the 64-bit stack
     * ------------------------------------------------------------
     */

    movq $stack_top, %rsp

    /*
     * Make absolutely sure the stack is 16-byte aligned.
     */

    andq $-16, %rsp


    /*
     * ------------------------------------------------------------
     * Load Multiboot arguments
     * ------------------------------------------------------------
     *
     * System V AMD64 ABI:
     *
     *   RDI = first argument
     *   RSI = second argument
     *
     * We stored the original 32-bit values in .bss.
     *
     * Writing to EDI/ESI automatically zero-extends into RDI/RSI.
     */

    movl boot_magic, %edi
    movl boot_info,  %esi


    /*
     * ------------------------------------------------------------
     * Call Rune
     * ------------------------------------------------------------
     *
     * Conceptually:
     *
     *     kernel_main(
     *         uint32_t magic,
     *         uint32_t multiboot_info
     *     );
     *
     * RDI = magic
     * RSI = multiboot information structure
     */

    call kernel_main


    /*
     * ------------------------------------------------------------
     * kernel_main should never return.
     * ------------------------------------------------------------
     */

.Lhalt:

    cli
    hlt
    jmp .Lhalt


    .size _start, . - _start
