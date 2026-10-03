# Comparison

Comparison yields a `bool`. Numbers, `bool`, `Character`, `String`, `CString`, pointers and payload-free enums all compare directly. Two `Option`s are equal when both are `None`, or both are `Some` of equal payloads, so `T?` has `==` exactly when `T` does. Structs, tuples and enums that carry payloads say what equal means with `bind operator::eq`.

**Comparing every comparable thing**

```rune
import std::io

enum Colour { Red, Green, Blue }

fn main() -> i64 {
    io::println(1 < 2)
    io::println(2.5 >= 2.5)
    io::println('a' < 'b')
    io::println("abc" == "abc")
    io::println("abc" < "abd")       // lexicographic, byte-wise
    io::println(Colour::Red == Colour::Red)
    io::println(Colour::Red != Colour::Blue)
    io::println(true > false)
    0
}
```
