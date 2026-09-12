# Placeholders

Each `{}` takes the next argument. `{{` and `}}` stand for literal braces. None of the three needs an import: their bodies name `std::io` in full.

**What a placeholder can hold**

```rune
fn main() -> i64 {
    let name = "world"
    let count = 3

    println!("hello {}", name)
    println!("{} + {} = {}", 1, 2, 1 + 2)
    println!("no placeholders at all")
    println!()                          // a blank line

    // A placeholder may name a variable directly, or an argument's position.
    println!("{count} of {name}")
    println!("{1} then {0}", "a", "b")

    // `{{` and `}}` are the literal braces.
    println!("{{not a placeholder}}")

    // print! is the same without the newline.
    print!("a")
    print!("b")
    println!()

    // format! returns the String instead of printing it.
    let line = format!("{} items", count)
    println!("{}", line.$length())
    0
}
```

| Placeholder | Takes |
| --- | --- |
| `{}` | the next argument in turn |
| `{0}`, `{1}` | the argument at that position |
| `{name}` | the variable `name`, from where the macro was used |
| `{{`, `}}` | a literal `{` or `}` |
