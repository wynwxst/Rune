# Bare-metal targets

Four foreign targets build with this machine's clang and ld.lld and are freestanding whatever the sources say.

| Name | Triple | Runs here with |
| --- | --- | --- |
| `bare-x86` | `i686-unknown-none-elf` | `qemu-system-i386 -kernel`: a multiboot kernel |
| `bare-x86_64` | `x86_64-unknown-none-elf` | — |
| `bare-arm64` | `aarch64-unknown-none-elf` | `qemu-system-aarch64 -M virt -kernel` |
| `bare-riscv64` | `riscv64-unknown-none-elf` | `qemu-system-riscv64 -M virt -bios none -kernel` |

A kernel says where its sections go with a linker script, and brings what has to run before any Rune can — the multiboot header, a stack — as an assembly file among its `c-sources`, which clang assembles like any other.

**examples/toyos/Rune.toml**

```toml
[build]
safety = "full"
runtime = "none"
entry = "none"
target = "bare-x86"
c-sources = ["boot/boot.s"]
linker-script = "kernel.ld"

[target.bare-x86]
runner = "qemu-system-i386 -display none -serial stdio -device isa-debug-exit,iobase=0xf4,iosize=0x04 -kernel"
```

```sh
$ cd examples/toyos
$ rune run                          # boots under QEMU, runs its checks, powers off
$ rune run -- -append panic         # trips a bounds check on purpose; exits 3
```

[`examples/toyos`](examples/toyos/README.md) is the whole of it worked through: a multiboot kernel with a VGA console and a serial one, a first-fit heap behind its `@allocator`, processes on a round-robin scheduler, each owned by the one before it, and 64-bit arithmetic on a 32-bit processor — all of it under `--safety full`.

[`examples/tetris-os`](examples/tetris-os/README.md) is a larger one: jdah's TETRIS-OS, ported from C. It boots from its own boot sector, sets up the IDT and the PICs, drives the timer, the keyboard, VGA mode 13h and a SoundBlaster 16 by DMA, and plays Tetris to the theme — Rune throughout but for the boot sector and the interrupt stubs. Porting it under `--safety full` turned up an out-of-bounds write and a missing table entry the C had carried silently. Its [build script](#buildscripts) lays the linked kernel out as a disk image, which is what `rune run` then boots.
