# Bindings in alternatives

Alternatives may bind, as long as every one of them binds the same names with the same types. That makes it possible to collapse variants that carry the same payload.

**`A(n) | B(n)` binds `n` either way**

```rune
import std::io

enum Token {
    Int(i64),
    Nat(i64),
    Word(String),
    Blank,
}

fn weight(t: Token) -> i64 {
    match t {
        Token::Int(n) | Token::Nat(n) => n,
        Token::Word(w) => w.$length(),
        Token::Blank => 0,
    }
}

enum Boxed { Small { size: i64 }, Large { size: i64 } }

fn size(b: Boxed) -> i64 {
    match b {
        Boxed::Small { size } | Boxed::Large { size } => size,
    }
}

fn main() -> i64 {
    io::println(weight(Token::Int(7)))
    io::println(weight(Token::Nat(9)))
    io::println(weight(Token::Word("abcd")))
    io::println(weight(Token::Blank))
    io::println(size(Boxed::Large { size: 12 }))
    0
}
```

**The same name must have the same type**

```rune
enum T { A(i64), B(String) }

fn f(t: T) -> i64 {
    match t {
        T::A(n) | T::B(n) => 0,
    }
}

fn main() -> i64 { 0 }
```
