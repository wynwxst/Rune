# Declaring a binding

There are four spellings, and they differ only in what they say out loud. A bare `name = value` declares a binding when nothing of that name is in scope; `let` says the same thing explicitly; `var` and `mut` are the two ways to ask for mutation.

**Every binding form**

```rune
import std::io

fn main() -> i64 {
    inferred = 7             // declares an immutable binding
    annotated: i64 = 7       // with the type written out
    let explicit = 7         // the same, said explicitly
    let typed: i64 = 7

    var counter = 0          // mutable
    mut also = 0             // `mut` is a synonym for `var`
    counter += 1
    also += 2

    var buffer: [4:u8]       // mutable and uninitialised

    io::println(inferred + annotated + explicit + typed)
    io::println(counter + also)
    io::println(buffer.$length())
    0
}
```
