# The macros the standard library provides

A macro is in scope by name alone, but its body is not: `vec!` needs `import std::collections::vector` because that is what its expansion mentions. The printing macros name `std::io` in full and so need nothing.

| Macro | Module | Does |
| --- | --- | --- |
| `println!("{}", x)` | `std::io` | writes a formatted line; `println!()` writes a blank one |
| `print!("{}", x)` | `std::io` | the same, without the newline |
| `format!("{}", x)` | the compiler | the `String` those two print; see **Formatting** |
| `vec!(a, b, c)` | `std::collections::vector` | a `Vector` of the values given, written out like an array literal; `vec!()` is an empty one |
| `assert!(cond)` | `std::testing` | checks it, and names the check after the expression itself |
| `assert!(cond, name)` | `std::testing` | the same, when the expression is not the explanation |
| `assertEq!(got, want)` | `std::testing` | compares, naming the check `got == want` |
| `assertEq!(got, want, name)` | `std::testing` | with a name of your own |
| `assertNot!(cond)` | `std::testing` | the negative of `assert!` |
| `stringify!(...)` | the compiler | the argument tokens as the text they were written as |

**Using them**

```rune
import std::testing
import std::collections::vector

fn twice(n: i64) -> i64 { n * 2 }

fn main() -> i64 {
    let v = vec!(1, 2, 3)

    assertEq!(v.length(), 3)
    assert!(twice(21) == 42)
    assertNot!(v.isEmpty())
    assert!(v.at(0).hasValue(), "the first element is there")

    testing::summary()
}
```

Each assertion takes its name from `stringify!`, so a failing line in a test says what was being checked without the check having been named twice. Where the expression is not the explanation, pass a name and it is used instead.
