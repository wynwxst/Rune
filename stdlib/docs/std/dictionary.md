# std::dictionary

`Map<K, V>` and `Set<T>`: open-addressed hash tables keyed *structurally*.
`mem::hash` and `mem::equals` work a key out from its layout, so any type at
all can be one — a `String`, a tuple, a struct — with no mark to bind.

## A map

`m[key]` reads and aborts on a missing key; `at` is the checked form.

```rune
import std::io
import std::dictionary

fn main() -> i64 {
    var ages = dictionary::Map<String, i64>()
    ages.put("ada", 36)
    ages["grace"] = 45
    io::println(ages["ada"])
    io::println(ages.at("nobody").isNil())
    io::println(ages.atOr("nobody", 0))
    io::println(ages.holds("grace"))
    io::println(ages.length())
    for (name, age) in ages {
        if age > 40 { io::println(name) }
    }
    io::println(ages.remove("ada") ?? 0)
    io::println(ages.length())
    0
}
```

## A set, and structural keys

```rune
import std::io
import std::dictionary

struct Cell { pub x: i64, pub y: i64 }

fn main() -> i64 {
    var seen = dictionary::Set<Cell>()
    io::println(seen.add(Cell { x: 1, y: 2 }))     // true: new
    io::println(seen.add(Cell { x: 1, y: 2 }))     // false: already there
    io::println(seen.holds(Cell { x: 1, y: 2 }))
    io::println(seen[Cell { x: 9, y: 9 }])         // membership by subscript
    let primes = dictionary::setOf([2, 3, 5])
    io::println(primes.length())
    let byName = dictionary::mapOf(["a", "b"], [1, 2])
    io::println(byName["b"])
    0
}
```
