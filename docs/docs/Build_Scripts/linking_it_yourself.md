# Linking it yourself

A target with `linker = "build-script"` has no linker of its own: the package's build script is called in its place, with exactly what a linker would get — the objects, `-o` and the file to write, the linker script, every `link-args` — in a third phase, `link`. What it does with them is its own business: hand them to a linker with some of its own, lay out an image directly, anything.

**Rune.toml**

```toml
[target.kernel]
base = "bare-x86"
cc = "i686-elf-gcc"
linker = "build-script"
linker-kind = "ld"          # hand me the flags as a linker takes them
```

**build.rune**

```text
import std::build

fn main() -> i64 {
    if build::linking() {
        var args = build::linkArguments()      // objects, -o out, -T kernel.ld
        args.push("--gc-sections")
        build::runAll("i686-elf-ld", &args)
    }
    0
}
```
