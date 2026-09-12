# Every kind of pattern

**Matching a pair, with a guard**

```rune
import std::io

enum Move { Rock, Paper, Scissors }

fn sameShape(a: Move, b: Move) -> bool { (a as i64) == (b as i64) }

fn beats(a: Move, b: Move) -> String {
    match (a, b) {
        (Move::Rock, Move::Scissors) => "rock wins",
        (Move::Paper, Move::Rock) => "paper wins",
        (Move::Scissors, Move::Paper) => "scissors win",
        (x, y) if sameShape(x, y) => "a draw",
        _ => "the other one wins",
    }
}

fn main() -> i64 {
    io::println(beats(Move::Rock, Move::Scissors))
    io::println(beats(Move::Rock, Move::Rock))
    io::println(beats(Move::Rock, Move::Paper))
    0
}
```

**Literals, ranges, alternatives, guards, destructuring**

```rune
import std::io

struct Point { x: i64, y: i64 }
enum Shape {
    Empty,
    Circle(f64),
    Rect { width: i64, height: i64 },
}

fn describeNumber(n: i64) -> String {
    match n {
        0 => "zero",                 // literal
        1 | 2 | 3 => "small",        // alternatives
        4..=9 => "single digit",     // inclusive range
        10..100 => "double digit",   // exclusive range
        x if x < 0 => "negative",    // guard
        _ => "large",                // wildcard
    }
}

fn describeShape(s: Shape) -> String {
    match s {
        Shape::Empty => "empty",
        Shape::Circle(r) => "circle r=" + r.$str(),
        Shape::Rect { width, height } => "rect " + width.$str() + "x" + height.$str(),
    }
}

fn describePoint(p: Point) -> String {
    match p {
        Point { x, y } => "(" + x.$str() + ", " + y.$str() + ")",
    }
}

fn describeTuple(t: (i64, String)) -> String {
    match t {
        (0, name) => "zero named " + name,
        (n, name) => name + "=" + n.$str(),
    }
}

fn main() -> i64 {
    io::println(describeNumber(0) + " " + describeNumber(2) + " " +
                describeNumber(7) + " " + describeNumber(42) + " " +
                describeNumber(-1) + " " + describeNumber(1000))
    io::println(describeShape(Shape::Empty))
    io::println(describeShape(Shape::Circle(1.5)))
    io::println(describeShape(Shape::Rect { width: 3, height: 4 }))
    io::println(describePoint(Point { x: 1, y: 2 }))
    io::println(describeTuple((0, "origin")))
    io::println(describeTuple((5, "five")))
    0
}
```

| Pattern | Matches |
| --- | --- |
| `_` | anything, binding nothing |
| `name` | anything, binding it to `name` |
| `var name` | anything, binding it mutably |
| `42` `"text"` `'c'` `true` | that exact value |
| `1 \| 2 \| 3` | any of the alternatives |
| `1..10` | the range, upper bound excluded |
| `1..=10` | the range, upper bound included |
| `Enum::Unit` | a variant with no payload |
| `Enum::Tuple(a, b)` | a tuple-shaped variant, binding its fields |
| `Enum::Struct { a, b }` | a struct-shaped variant, binding by name |
| `Struct { a, b }` | a struct, binding its fields |
| `Struct { a, .. }` | a struct, ignoring the rest |
| `(a, b)` | a tuple of that arity |
| `[a, b, c]` | an array or slice of exactly three, binding each |
| `[first, ..rest]` | one or more, binding the remainder as a slice |
| `[first, .., last]` | two or more, ignoring the middle |
| `&inner` | through a borrow |
| `name @ 1..10` | the range, and binds the whole value too |
| `pattern if cond` | the pattern, when the guard also holds |

*Every pattern form*
