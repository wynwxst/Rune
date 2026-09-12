# Equality and ordering

**One `eq` and one `cmp` cover six operators**

```rune
import std::io

struct Version { major: i64, minor: i64 }

bind operator::eq to Version {
    fn eq(&self, rhs: &Version) -> bool {
        self.major == rhs.major && self.minor == rhs.minor
    }
}

bind operator::cmp to Version {
    fn cmp(&self, rhs: &Version) -> i64 {
        if self.major != rhs.major { return self.major - rhs.major }
        self.minor - rhs.minor
    }
}

fn main() -> i64 {
    let a = Version { major: 1, minor: 2 }
    let b = Version { major: 1, minor: 9 }

    io::println(a == a)
    io::println(a != b)      // `eq`, inverted
    io::println(a < b)       // all four come from `cmp`
    io::println(a <= b)
    io::println(b > a)
    io::println(b >= b)
    0
}
```
