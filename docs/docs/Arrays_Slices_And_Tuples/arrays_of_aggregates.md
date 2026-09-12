# Arrays of aggregates

**Structs and tuples inside an array**

```rune
import std::io

struct Reading { label: String, value: f64 }

fn main() -> i64 {
    let readings: [3:Reading] = [
        Reading { label: "morning", value: 12.5 },
        Reading { label: "noon", value: 21.0 },
        Reading { label: "evening", value: 15.5 },
    ]

    var warmest = readings[0]
    for r in readings {
        if r.value > warmest.value { warmest = r }
    }
    io::println(warmest.label + " " + warmest.value.$str())

    let pairs: [2:(i64, i64)] = [(1, 2), (3, 4)]
    for (a, b) in pairs {
        io::println((a * b).$str())
    }
    0
}
```
