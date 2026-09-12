# Callbacks

`@cfunction(...)` is a bare C function pointer: a code address and nothing else. A top-level `fn` has no captured state, so its address alone is one, and it converts wherever one is wanted — which is what lets C drive Rune code.

**C's qsort, sorting with a Rune comparator**

```rune
import std::io

extern "C" {
    fn qsort(base: *var u8, count: u64, size: u64,
             compare: @cfunction(*u8, *u8) -> i32)
}

/// qsort hands the comparator two pointers into the array it is sorting.
fn ascending(a: *u8, b: *u8) -> i32 {
    let x = unsafe { (a as *i64)[0] }
    let y = unsafe { (b as *i64)[0] }
    if x < y { return -1 }
    if x > y { return 1 }
    0
}

fn main() -> i64 {
    var values: [5:i64] = [42, 7, 19, 3, 25]
    unsafe { qsort(&var values[0] as *var u8, 5 as u64, 8 as u64, ascending) }

    var out = ""
    for v in values { out += v.$str() + " " }
    io::println(out)
    0
}
```

> [!WARNING]
> **`@function` is not `@cfunction`**
>
> `@function` is the *closure* type: code plus a captured environment, two words wide. C has nowhere to put the second, so writing one in an `extern` signature is an error rather than a wrong answer at run time. A closure genuinely cannot be a C callback — pass a top-level `fn`, and give C any state it needs through the `void *` such APIs usually carry for the purpose.
