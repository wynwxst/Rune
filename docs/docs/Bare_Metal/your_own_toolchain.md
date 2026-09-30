# Your own toolchain

Rune compiles Rune; everything around it — assembling the boot code, compiling C, linking, laying out an image — is done by tools you choose, and nothing the build adds assumes which. Name a `cc` and a `linker` for the target, and that is what runs:

**GNU cross tools, as on macOS**

```text
[target.i686-elf]
base = "bare-x86"
cc = "i686-elf-gcc"         # assembles boot.s, compiles any C
linker = "i686-elf-ld"      # links, as a linker: -T kernel.ld, not -Wl,-T,...
c-flags = ["-march=i686"]   # whatever else your system needs
link-args = ["-Map=kernel.map"]
```

```sh
$ rune targets
  i686-elf        i686-unknown-none-elf
      ✓ builds with i686-elf-gcc
        links with i686-elf-ld (ld)
        c-flags   -ffreestanding -fno-pic -march=i686
        link-args -Map=kernel.map
$ rune run --target i686-elf
```

| Rule | So |
| --- | --- |
| A foreign target's clang flags (`--target=`, `-fuse-ld=lld`) belong to its clang | naming your own `cc` drops the compile ones; naming your own `cc` or `linker` drops the link ones |
| What any bare-metal compiler takes stays: `-ffreestanding`, `-fno-pic` | `default-flags = false` drops those too, and every flag the compiler would add to the link |
| A linker is told things a linker's way | `linker-kind = "ld"` (worked out from names like `ld`, `i686-elf-ld`, `ld.lld`): `-T script`, and nothing only a compiler driver understands — no `-nostdlib`, no `-Wl,` |
| The rest is yours | `c-flags` and `link-args` add anything; `linker = "build-script"` hands the link to [build.rune](#buildscripts); a finish step there makes an image, signs it, anything |

`rune targets` prints what each target compiles and links with, flags included, and `rune build -v` every command it runs, so nothing is hidden.
