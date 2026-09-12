# Globals

`global` declares a binding at module scope. It needs either a type or an initialiser, and initialisers run once, before `main`, in declaration order.

**Module-level state**

```rune
import std::io

global var counter: i64 = 0
global GREETING: String = "set up before main runs"
global LIMIT: i64 = 4 * 4

fn bump() -> i64 {
    counter += 1
    counter
}

fn main() -> i64 {
    bump()
    bump()
    io::println(GREETING)
    io::println(counter)
    io::println(LIMIT)
    0
}
```

Inside a function, `global name: T` refers to a module-level binding rather than declaring a local one.
