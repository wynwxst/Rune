#!/bin/sh
# `rune run`'s runner: turns the kernel ELF that `rune build` made into the
# disk image the boot sector expects — sector 0 the boot sector, the kernel
# from sector 1, each where link.ld's AT(...) says — and boots it.
#
# The image is made here rather than by the linker because ld.lld writes
# `--oformat=binary` by virtual address, and the boot sector (at 0x7C00)
# and the kernel (at 0x10000) are not where they sit on the disk.
# llvm-objcopy lays a binary out by load address, which is what is wanted.
#
#     sh tools/run.sh <kernel> [qemu arguments...]
set -e
kernel="$1"
shift
image="$kernel.img"
if command -v llvm-objcopy >/dev/null 2>&1; then
    llvm-objcopy -O binary "$kernel" "$image"
else
    objcopy -O binary "$kernel" "$image"
fi
exec qemu-system-i386 \
    -drive format=raw,file="$image" \
    -audiodev none,id=snd -device sb16,audiodev=snd \
    "$@"
