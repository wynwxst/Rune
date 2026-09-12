# Assignment

Every binary operator that makes sense has a compound form. A compound assignment follows exactly the rules of the operator it names, including operator overloads.

**Every compound assignment**

```rune
import std::io

fn main() -> i64 {
    var n = 10
    n += 5;  io::println(n)
    n -= 3;  io::println(n)
    n *= 2;  io::println(n)
    n /= 4;  io::println(n)
    n %= 4;  io::println(n)
    n <<= 4; io::println(n)
    n >>= 2; io::println(n)
    n &= 12; io::println(n)
    n |= 3;  io::println(n)
    n ^= 5;  io::println(n)

    var text = "grow"
    text += "ing"
    io::println(text)
    0
}
```
