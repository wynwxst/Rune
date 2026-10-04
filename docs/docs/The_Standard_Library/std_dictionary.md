# std::dictionary

Keyed lookup: a `Map` from keys to values, and a `Set` of keys alone. Both are open-addressed hash tables with linear probing, backed by three `mem::Buffer`s — one for keys, one for values, and one for the state of each slot. They double when they pass two thirds full.

**A map and a set**

```rune
import std::io
import std::dictionary

fn main() -> i64 {
    var ages = dictionary::Map<String, i64>()
    ages.put("ada", 36)
    ages.put("grace", 45)

    match ages.at("ada") {
        Some(n) => io::println("ada is " + n.$str()),
        None    => io::println("no ada"),
    }
    io::println("holds grace: " + ages.holds("grace").$str())
    io::println("unknown: " + ages.atOr("nobody", -1).$str())

    let seen = dictionary::setOf(["a", "b", "a", "c"])
    io::println("distinct: " + seen.length().$str())
    0
}
```

> [!NOTE]
> **What makes two keys the same**
>
> A key is hashed and compared **structurally**, by `mem::hash` and `mem::equals`. The compiler works both out from the layout, so a key needs nothing of its own: no `Hashable` mark to bind, and no `Display` standing in for one. Any type at all can be a key — a struct, an enum with payloads, a tuple, a class.

| Key type | Two keys are the same when |
| --- | --- |
| integers, floats, `bool`, `Character` | the values are equal |
| `String`, `CString` | the **contents** match, not the address |
| a class | they are the **same object** — two objects with equal fields are two keys |
| struct, tuple, array | every part matches, part by part |
| an enum | the variant matches, and its payload does |

| Map method | Signature | Does |
| --- | --- | --- |
| `length` / `isEmpty` | `(&self) -> ...` |  |
| `at` | `(&self, key: K) -> V?` | the value, or nothing |
| `holds` | `(&self, key: K) -> bool` |  |
| `atOr` | `(&self, key: K, fallback: V) -> V` | the value, or `fallback` |
| `put` | `(&var self, key: K, value: V) -> V?` | stores; returns what it replaced |
| `remove` | `(&var self, key: K) -> V?` | returns what it held |
| `clear` | `(&var self)` | drops everything, keeps the storage |
| `keysOf` / `valuesOf` | `(&self) -> Vector<...>` | everything, in table order |

| Set method | Signature | Does |
| --- | --- | --- |
| `length` / `isEmpty` | `(&self) -> ...` |  |
| `holds` | `(&self, value: T) -> bool` |  |
| `add` | `(&var self, value: T) -> bool` | false when already there |
| `remove` | `(&var self, value: T) -> bool` | false when it was not |
| `membersOf` | `(&self) -> Vector<T>` | everything, in table order |

| Written | Means | When it is not there |
| --- | --- | --- |
| `m[key]` | the value | **aborts** — `at` answers instead |
| `m[key] = v` | stores it | — |
| `m[key] += v` | reads, adds, stores | aborts on the read |
| `s[value]` | whether it is a member | `false`; a Set has nothing to store, so there is no `s[v] = ...` |

Both iterate. A `Map` produces `(key, value)` pairs and a `Set` produces its members, so the `std::iter` adaptors work on either:

**Subscripting and walking**

```rune
import std::io
import std::dictionary

fn main() -> i64 {
    var ages = dictionary::Map<String, i64>()
    ages["ada"] = 36
    ages["grace"] = 45
    ages["ada"] += 1

    var total = 0
    for (name, age) in ages { total += age }
    io::println("total " + total.$str())

    let seen = dictionary::setOf(["a", "b", "c"])
    io::println("holds b: " + seen["b"].$str())
    0
}
```

> [!WARNING]
> **Order is not promised**
>
> `keysOf`, `valuesOf` and `membersOf` hand back whatever order the table happens to hold — not insertion order, and not stable across a resize. Sort the result when the order matters.

> [!NOTE]
> **Why removal leaves a mark**
>
> A removed slot becomes a tombstone rather than an empty one. Emptying it would cut the probe chain for any key that walked past it on its way in, and those keys would silently stop being found.

`Set<T>` is a `Map<T, bool>` whose values are all true, which is what a set is — so there is one probing implementation rather than two. `dictionary::setOf(values)` and `dictionary::mapOf(keys, values)` build one from a slice.

A map is common enough to be worth writing short. `[K:V]` is the type and `[key: value, ...]` is the value, with `[:]` for the empty one — `dictionary::emptyMap<K, V>()` written short, taking its types from where it is going:

**Written short**

```rune
import std::io

fn count(m: [String:i64]) -> i64 { m.length() }

fn main() -> i64 {
    let ages: [String:i64] = ["ada": 36, "bob": 41]
    io::println(ages.at("ada").or(0))
    io::println(count(ages))

    // Pairs may go on their own lines.
    let words = [
        "one": 1,
        "two": 2,
    ]
    io::println(words.at("two").or(0))

    // `[:]` takes its types from where it is going.
    let empty: [String:i64] = [:]
    io::println(empty.length())
    0
}
```

> [!NOTE]
> **How it is told from an array**
>
> `[3:i64]` is an array of three and `[String:i64]` is a map: an array's length is a **number** and a map's key is a **type**, so what was meant is decided by what the name means rather than by the punctuation. `[SIZE:i64]` with `SIZE` a constant is still an array.
