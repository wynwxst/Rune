# Static methods

A method without a `self` parameter belongs to the type rather than an instance, and is called through the type name.

**Constructors that are not `init`**

```rune
import std::io

class Temperature {
    pub celsius: f64
    fn init(self, celsius: f64) { self.celsius = celsius }

    pub fn fromFahrenheit(f: f64) -> Temperature {
        Temperature((f - 32.0) / 1.8)
    }
    pub fn freezing() -> Temperature { Temperature(0.0) }
}

fn main() -> i64 {
    io::println(Temperature::fromFahrenheit(212.0).celsius)
    io::println(Temperature::freezing().celsius)
    0
}
```
