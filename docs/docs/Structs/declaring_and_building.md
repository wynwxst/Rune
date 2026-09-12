# Declaring and building

**Fields, defaults and literals**

```rune
import std::io

struct Point {
    pub x: f64
    pub y: f64
}

struct Config {
    pub name: String
    pub retries: i64 = 3          // a field default
    pub verbose: bool = false
}

fn main() -> i64 {
    let origin = Point { x: 0.0, y: 0.0 }
    let p = Point { x: 3.0, y: 4.0 }

    let quiet = Config { name: "quiet" }               // defaults fill in
    let loud = Config { name: "loud", verbose: true }

    io::println(p.x.$str() + "," + p.y.$str())
    io::println(origin.x)
    io::println(quiet.name + " " + quiet.retries.$str() + " " + quiet.verbose.$str())
    io::println(loud.verbose)
    0
}
```
