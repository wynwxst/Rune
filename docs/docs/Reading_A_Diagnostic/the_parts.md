# The parts

| Part | Looks like | Means |
| --- | --- | --- |
| header | `┌ ERROR[E0230] in file.rune` | severity, code, file |
| gutter | `12 ║` | the line number, then your source |
| carets | `^^^^` | exactly the span at fault |
| `note:` | `● note: …` | why the compiler thinks so |
| `hint:` | `○ hint: …` | something you could try |
| related | `└▶ …` | a second place that matters |

A type mismatch is the shape to learn first: what was wanted, what arrived, and where each came from.

**A type mismatch**

```rune
fn area(width: i64, height: i64) -> i64 {
    width * height
}

fn main() -> i64 {
    area(3, "four")
}
```

When the mistake has an obvious repair, the hint says it outright.

**A hint that names the fix**

```rune
struct Point { x: i64, y: i64 }

fn main() -> i64 {
    let p = Point { x: 1, y: 2 }
    p.z
}
```

A second location appears as a related note whenever the explanation is somewhere other than the error.

**Assigning to an immutable binding**

```rune
fn main() -> i64 {
    let total = 10
    total = 11
    total
}
```

Inside a generic, the failure is in the template but the *cause* is the call. Both are shown.

**A failure inside an instantiation**

```rune
fn largest<T: Comparable>(a: T, b: T) -> T {
    if a > b { a } else { b }
}

struct Opaque { tag: i64 }

fn main() -> i64 {
    let winner = largest(Opaque { tag: 1 }, Opaque { tag: 2 })
    winner.tag
}
```

Exhaustiveness is checked, and the message lists precisely what you left out.

**A match with a gap**

```rune
enum Signal { Red, Amber, Green }

fn main() -> i64 {
    let s = Signal::Amber
    match s {
        Signal::Red => 0
        Signal::Green => 1
    }
}
```
