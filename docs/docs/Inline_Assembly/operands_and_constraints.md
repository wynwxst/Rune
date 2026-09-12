# Operands and constraints

`$0` is the result where there is one and the first input where there is not; the inputs follow in the order they were written. The constraint string lists one constraint per operand, comma separated, results first — it is the notation LLVM and GCC share.

| Constraint | Means |
| --- | --- |
| `r` | an input in a general register |
| `=r` | a result written to a general register |
| `0` | this input must land where operand 0 did |
| `i` | an immediate the assembler can fold in |
| `m` | a memory operand |

LLVM is asked whether the constraints fit the call before anything is emitted, so a mismatch is a diagnostic pointing at the call rather than a failure inside the back end.

**Constraints that do not fit**

```rune
import std::asm

fn broken() -> i64 {
    // Three inputs promised, none supplied.
    unsafe { asm::value<i64>("nop", "=r,r,r,r") }
}
```
