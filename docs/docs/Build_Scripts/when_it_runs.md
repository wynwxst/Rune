# When it runs

The script is compiled again only when it changes, and runs on every build, so what it does should be quick, or look for itself whether there is anything to do. What its prepare phase answers is part of what decides whether the package is compiled again, so a script that answers the same as last time costs a compile nothing.

> [!NOTE]
> **Hosted, always**
>
> A build script is compiled for this machine with its own defaults — hosted, with the standard library — even when the package is a freestanding kernel for another processor. It can read files, run programs and print; the package it builds is what cannot.

[`examples/tetris-os`](examples/tetris-os/README.md) uses one to turn its linked kernel into the disk image its boot sector expects, and its runner boots that image:

**examples/tetris-os/Rune.toml**

```toml
[target.bare-x86]
runner = "qemu-system-i386 -drive format=raw,file={} -audiodev none,id=snd -device sb16,audiodev=snd"
```
