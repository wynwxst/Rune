# Comparison and sorting

**Ordering strings**

```rune
import std::io

fn main() -> i64 {
    io::println("abc" == "abc")
    io::println("abc" != "abd")
    io::println("abc" < "abd")        // byte-wise, so ASCII order
    io::println("Z" < "a")            // uppercase sorts first
    io::println("ab" < "abc")         // a prefix sorts first

    var names: [4:String] = ["pear", "apple", "fig", "date"]
    // A simple insertion sort, to show the comparisons at work.
    for i in 1..4 {
        var j = i
        while j > 0 {
            if names[j] < names[j - 1] {
                let hold = names[j]
                names[j] = names[j - 1]
                names[j - 1] = hold
            }
            j -= 1
        }
    }
    var out = ""
    for n in names { out += n + " " }
    io::println(out)
    0
}
```
