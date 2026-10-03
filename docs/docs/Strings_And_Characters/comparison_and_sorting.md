# Comparison and sorting

**Ordering strings**

```rune
import std::io
import std::collections::slice

fn main() -> i64 {
    io::println("abc" == "abc")
    io::println("abc" != "abd")
    io::println("abc" < "abd")        // byte-wise, so ASCII order
    io::println("Z" < "a")            // uppercase sorts first
    io::println("ab" < "abc")         // a prefix sorts first

    let names: [4:String] = ["pear", "apple", "fig", "date"]
    // `<` is all a sort needs to be told.
    let sorted = slice::sortedBy(names, ||(a: String, b: String) -> bool { a < b })
    var out = ""
    for n in sorted { out += n + " " }
    io::println(out)
    0
}
```
