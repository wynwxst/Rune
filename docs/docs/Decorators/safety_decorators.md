# Safety decorators

**#unsafe and #safe**

```rune
import std::io

#unsafe
fn trustMe(p: *i64) -> i64 { *p }

#safe("the pointer comes from `&var` on a live local, so it cannot dangle")
fn readLocal() -> i64 {
    var value = 41
    unsafe { trustMe(&var value as *i64) + 1 }
}

fn main() -> i64 {
    io::println(readLocal().$str())
    0
}
```
