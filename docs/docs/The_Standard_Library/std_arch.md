# std::arch

What the machine being built **for** is like. Everything here is settled while compiling, out of the target triple: a type alias is the type it names, and a number is in the object file as that number. `arch::bits` is the constant `64` on a 64-bit target, not something worked out at startup, so `if arch::is32Bit { ... }` folds away entirely on the other one.

A cross build answers for the target, never for the machine doing the compiling — which is the whole reason to ask here rather than at run time.

| Name | Is | On a 64-bit target |
| --- | --- | --- |
| `size` | `type` | `i64` — signed, pointer-sized: an offset, a difference, an index |
| `usize` | `type` | `u64` — unsigned, pointer-sized: a length or a count, and what `mem::size_of` hands back |
| `float` | `type` | `f64` — a *size* rule, not a speed one |
| `bits` | `i64` | `64` |
| `pointerSize` | `i64` | `8` |
| `is64Bit` / `is32Bit` | `bool` | `true` / `false` |
| `endian` | `String` | `"little"` or `"big"` |
| `littleEndian` / `bigEndian` | `bool` | the same, to branch on |
| `name` | `String` | `aarch64`, `x86_64`, `x86`, `arm`, `riscv32`, `riscv64`, `wasm32`, `wasm64`, `powerpc64`, or `unknown` |
| `os` | `String` | `macos`, `windows`, `linux`, `ios`, `android`, `freebsd`, `openbsd`, `netbsd`, `solaris`, `wasi`, or `unknown` |
| `family` | `String` | `unix`, `windows` or `wasm` |
| `triple()` | `fn -> String` | the whole triple as LLVM normalised it — `arm64-apple-macosx15.0.0`, `x86_64-w64-windows-gnu` |

**Asking about the target**

```rune
import std::io
import std::arch

fn main() -> i64 {
    // A type alias *is* the type, so this is the machine word.
    let index: arch::size = 3
    let count: arch::usize = 10 as arch::usize
    io::println((index + 1).$str())
    io::println(count.$str())

    io::println(arch::bits.$str() + "-bit " + arch::name)
    io::println(arch::os + " (" + arch::family + ")")
    io::println(arch::endian + "-endian")

    // Folded to one branch: the other is not in the binary at all.
    if arch::is64Bit { io::println("wide pointers") }
    else { io::println("narrow pointers") }
    0
}
```

`triple()` is the one answer that is not a fixed list, so it is the compiler's own rather than a `#Config` branch. It still costs nothing: the string is in the object file.

> [!NOTE]
> **`#Config` or `std::arch`?**
>
> These are the same answers `#Config` gives, in a form you can compute with. Reach for `#Config` when a declaration should not **exist** on a target — a function that calls something only Windows has — and for `std::arch` when a value or a type depends on it.
