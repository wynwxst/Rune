/* The one piece of the kernel that cannot be Rune: before this runs there
 * is no stack, and a Rune function — any function — needs one. So: the
 * multiboot header QEMU looks for, 64 KB of stack, and a call into Rune
 * with the two things the loader left in registers. */

    .set MAGIC,    0x1BADB002
    .set FLAGS,    (1 << 0) | (1 << 1)      /* page-align modules; memory info */
    .set CHECKSUM, -(MAGIC + FLAGS)

    .section .multiboot, "a"
    .align 4
    .long MAGIC
    .long FLAGS
    .long CHECKSUM

    .section .bss
    .align 16
stack_bottom:
    .skip 65536
stack_top:

    .section .text
    .global _start
    .type _start, @function
_start:
    mov $stack_top, %esp
    push %ebx                   /* the multiboot information structure */
    push %eax                   /* the magic number that says it is one */
    call kernel_main            /* src/main.rune; never returns */
1:  cli
    hlt
    jmp 1b
