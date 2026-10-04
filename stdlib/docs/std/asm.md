# std::asm

Instructions handed to the assembler as written. `run` is for instructions
written for their effects and is never elided; `value<R>` is treated as a
pure function of its inputs. Both are `#unsafe`, and both want a `#Config`
around them, because an instruction is for one architecture.

## One instruction

```rune
import std::io
import std::asm

#Config(arch == "aarch64")
fn addUp(a: i64, b: i64) -> i64 {
    unsafe { asm::value<i64>("add $0, $1, $2", "=r,r,r", a, b) }
}

#Config(arch == "x86_64")
fn addUp(a: i64, b: i64) -> i64 {
    unsafe { asm::value<i64>("lea ($1, $2), $0", "=r,r,r", a, b) }
}

#Config(arch != "aarch64" && arch != "x86_64")
fn addUp(a: i64, b: i64) -> i64 { a + b }

fn main() -> i64 {
    io::println(addUp(40, 2))
    0
}
```
