# `@safe("reason")`

**An unchecked core behind a checked edge**

```rune
import std::io
import std::mem

// The pattern: an unchecked core, a checked edge, and a reason on the seam
// saying why the edge is enough.
@unsafe
fn sumUnchecked(cells: *var i64, count: i64) -> i64 {
    var total = 0
    var i = 0
    while i < count {
        total += cells[i]
        i += 1
    }
    total
}

@safe("the block is sized for `count` cells and every one is written below")
fn sumOfSquares(count: i64) -> i64 {
    if count <= 0 { return 0 }
    let block = mem::allocator.allocate(count as usize * mem::size_of<i64>())
    if mem::isNull(block) { return 0 }
    let cells = unsafe { block as *var i64 }
    var i = 0
    while i < count {
        unsafe { cells[i] = (i + 1) * (i + 1) }
        i += 1
    }
    let total = unsafe { sumUnchecked(cells, count) }
    mem::allocator.deallocate(block)
    total
}

fn main() -> i64 {
    io::println(sumOfSquares(4).$str())
    io::println(sumOfSquares(0).$str())
    0
}
```

Some functions are safe *because of an argument you can make*, not because the compiler proved it. `@safe` records that argument. It permits the function to expose a checked interface over an unchecked implementation, and the string is kept with the declaration.

**A checked wrapper over an unchecked core**

```rune
import std::io

@unsafe
fn divideUnchecked(a: i64, b: i64) -> i64 { a / b }

@safe("the divisor is compared against zero on the line above")
fn divide(a: i64, b: i64) -> i64? {
    if b == 0 { return nil }
    unsafe { divideUnchecked(a, b) }
}

fn main() -> i64 {
    io::println(divide(84, 2).or(0).$str())
    io::println(divide(84, 0).hasValue().$str())
    0
}
```

> [!WARNING]
> **Say why**
>
> `@safe` without a reason is accepted but warns. A justification nobody wrote down is a justification nobody can check.
