# Your first program

A program is a module with a `main`. The result becomes the process exit status.

**hello.rune**

```rune
import std::io

fn main() -> i64 {
    io::println("Hello from Rune!")
    0
}
```

```sh
runec -o hello hello.rune && ./hello
```
