# Two calls

| Call | Does |
| --- | --- |
| `asm::run(text, constraints, ...)` | Runs the instructions for their effects. Never dropped, never merged with an identical call. |
| `asm::value<R>(text, constraints, ...) -> R` | Runs them and takes the result as `R`. Treated as a pure function of its inputs. |

Both are `#unsafe`, so a call needs `unsafe { ... }` or a caller that is itself `#unsafe`. The instructions and the constraints have to be written out at the call: they are assembled with the program, so they cannot be computed while it runs.

**One instruction, per architecture**

```rune
import std::io
import std::asm

#Config(arch == "aarch64")
fn addUp(a: i64, b: i64) -> i64 {
    unsafe { asm::value<i64>("add $0, $1, $2", "=r,r,r", a, b) }
}

#Config(arch == "x86_64")
fn addUp(a: i64, b: i64) -> i64 {
    unsafe { asm::value<i64>("addq $2, $0", "=r,0,r", a, b) }
}

#Config(arch != "aarch64" && arch != "x86_64")
fn addUp(a: i64, b: i64) -> i64 { a + b }

fn main() -> i64 {
    io::println(addUp(2, 40).$str())
    0
}
```
