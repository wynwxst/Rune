# Macros

A macro is a rewrite from one run of tokens to another, chosen by pattern. Expansion happens before anything is parsed, so a macro can stand for whatever the grammar accepts — an expression, a run of statements, a declaration.

**Definitions, rules and repetition**

```rune
import std::io
import std::collections::vector

macro twice {
    ($x: expr) => { ($x) + ($x) }
}

macro greet {
    ($name: expr) => { io::println("hello " + $name) }
    ($name: expr, $times: expr) => {
        var i = 0
        while i < $times { io::println("hi " + $name); i += 1 }
    }
}

macro ints {
    ($($item: expr),*) => {
        {
            let built = vector::Vector<i64>()
            $( built.push($item); )*
            built
        }
    }
}

fn main() -> i64 {
    // A one-expression macro splices as a unit, so it composes.
    io::println((twice!(3) + 1).$str())

    // Rules are tried in order; the arguments pick one.
    greet!("ada")
    greet!("bob", 2)

    // A repetition, and the compiler's own `stringify!`.
    io::println(ints!(1, 2, 3).length().$str())
    io::println(stringify!(x > 0 && x != 3))
    0
}
```

A definition is `macro name { (pattern) => { expansion } }`, with as many rules as you like. They are tried in order and the first whose pattern fits the arguments wins. An invocation carries a `!`, so a reader always knows expansion is happening.

> [!NOTE]
> **Where a macro is in scope**
>
> Definitions are gathered from every file before any is parsed, so a macro may be used above where it is written and across module boundaries — `vec!` lives in `std::collections::vector` and works anywhere.

## Pages

- [Patterns](patterns.md)
- [Folding a repetition](folding_a_repetition.md)
- [How an expansion is spliced](how_an_expansion_is_spliced.md)
- [The macros the compiler supplies](the_macros_the_compiler_supplies.md)
- [stringify!](stringify.md)
- [Macros that declare](macros_that_declare.md)
- [When an expansion is wrong](when_an_expansion_is_wrong.md)
- [Visibility](visibility.md)
- [The macros the standard library provides](the_macros_the_standard_library_provides.md)
- [What is reported](what_is_reported.md)
- [When a pattern is not enough](when_a_pattern_is_not_enough.md)
- [What a macro is given](what_a_macro_is_given.md)
- [What is reported, for these](what_is_reported_for_these.md)
