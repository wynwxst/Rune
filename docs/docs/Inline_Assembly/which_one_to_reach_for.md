# Which one to reach for

`asm::value` is a function of its inputs, so the compiler may drop the call when nothing uses the result and keep one copy where the same call appears twice on the same values. That is right for arithmetic and wrong for anything that answers differently each time — a clock, a counter, a device register. Use `asm::run` for those, and for barriers, fences and syscalls.

**Written for its effects**

```rune
import std::asm

#Config(arch == "aarch64")
fn barrier() { unsafe { asm::run("dmb ish", "") } }

#Config(arch == "x86_64")
fn barrier() { unsafe { asm::run("mfence", "") } }
```

> [!WARNING]
> **Always paired with `#Config`**
>
> Assembly is text for one machine. Anything using it wants a `#Config(arch == "...")` around it and a definition for the architectures you do not handle — otherwise the build fails on the first machine nobody thought about.
