# Super-marks

A mark may require other marks. Binding it then requires those too.

**A mark that builds on another**

```rune
import std::io

mark Named {
    fn name(&self) -> String
}

mark Measured: Named {              // requires Named as well
    fn size(&self) -> f64
    fn summary(&self) -> String {
        self.name() + " is " + self.size().$str()
    }
}

struct Crate { label: String, volume: f64 }

bind Named to Crate {
    fn name(&self) -> String { self.label.$clone() }
}

bind Measured to Crate {
    fn size(&self) -> f64 { self.volume }
}

fn main() -> i64 {
    io::println(Crate { label: "box", volume: 2.5 }.summary())
    0
}
```

**The super-mark is not satisfied**

```rune
mark Named { fn name(&self) -> String }
mark Measured: Named { fn size(&self) -> f64 }

struct Crate { volume: f64 }

bind Measured to Crate {
    fn size(&self) -> f64 { self.volume }
}

fn main() -> i64 { 0 }
```
