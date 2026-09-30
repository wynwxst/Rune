# `into`: conversions you write yourself

`as` has fixed meanings — numbers, enums, pointers — and no binding can change them. A conversion between two of your own types is `into`, which dispatches through the built-in `As` mark. `bind Celsius into Fahrenheit` is the same as `bind As<Fahrenheit> to Celsius`: the binding reads the same way as the value, `c into Fahrenheit`. One source may convert into as many destinations as it likes.

**One source, several destinations**

```rune
import std::io

struct Celsius { v: f64 }
struct Fahrenheit { v: f64 }
struct Kelvin { v: f64 }

bind Celsius into Fahrenheit {
    fn convert(&self) -> Fahrenheit { Fahrenheit { v: self.v * 1.8 + 32.0 } }
}
bind Celsius into Kelvin {
    fn convert(&self) -> Kelvin { Kelvin { v: self.v + 273.15 } }
}
bind Fahrenheit into Celsius {
    fn convert(&self) -> Celsius { Celsius { v: (self.v - 32.0) / 1.8 } }
}
bind Kelvin into Fahrenheit {
    fn convert(&self) -> Fahrenheit {
        Fahrenheit { v: (self.v - 273.15) * 1.8 + 32.0 }
    }
}

fn main() -> i64 {
    let c = Celsius { v: 100.0 }
    io::println((c into Fahrenheit).v.$str())
    io::println((c into Kelvin).v.$str())

    // They compose, left to right.
    io::println((c into Kelvin into Fahrenheit into Celsius).v.$str())
    0
}
```

Because `As<Target>` is an ordinary generic mark, it works as a bound: a function can require that whatever it is given converts into the type it needs.

**`As<T>` as a bound**

```rune
import std::io

struct Celsius { v: f64 }
struct Kelvin { v: f64 }
struct Fahrenheit { v: f64 }

bind Celsius into Fahrenheit {
    fn convert(&self) -> Fahrenheit { Fahrenheit { v: self.v * 1.8 + 32.0 } }
}
bind Kelvin into Fahrenheit {
    fn convert(&self) -> Fahrenheit {
        Fahrenheit { v: (self.v - 273.15) * 1.8 + 32.0 }
    }
}

fn isWarm<T: As<Fahrenheit>>(v: T) -> bool { (v into Fahrenheit).v > 80.0 }

fn main() -> i64 {
    io::println(isWarm(Celsius { v: 30.0 }).$str())
    io::println(isWarm(Kelvin { v: 320.0 }).$str())
    0
}
```

**`as` will not reach it**

```rune
struct Celsius { v: f64 }
struct Fahrenheit { v: f64 }

bind Celsius into Fahrenheit {
    fn convert(&self) -> Fahrenheit { Fahrenheit { v: self.v * 1.8 + 32.0 } }
}

fn main() -> i64 {
    let c = Celsius { v: 100.0 }
    let f = c as Fahrenheit
    0
}
```

**No conversion defined**

```rune
struct A { v: i64 }
struct B { v: i64 }

fn main() -> i64 {
    let a = A { v: 1 }
    let b = a into B
    b.v
}
```
