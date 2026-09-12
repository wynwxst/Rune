# Inheritance and `super`

**A base method reaching an overridden one**

```rune
import std::io

class Shape {
    name: String
    fn init(self, name: String) { self.name = name }
    pub fn area(&self) -> f64 { 0.0 }
    pub fn describe(&self) -> String {
        self.name + " of area " + self.area().$str()
    }
}

class Square: Shape {
    side: f64
    fn init(self, side: f64) {
        super.init("square")
        self.side = side
    }
    pub fn area(&self) -> f64 { self.side * self.side }
}

fn main() -> i64 {
    let s = Square(3.0)
    // `describe` is the base's, but the `area` it reaches is the subclass's.
    io::println(s.describe())
    0
}
```

A class may name one base class after a colon. A method with the same name overrides the base version and is dispatched dynamically; `super.method()` calls the base version directly.

**Overriding, and reaching the base implementation**

```rune
import std::io

class Animal {
    pub name: String
    fn init(self, name: String) { self.name = name }
    pub fn speak(&self) -> String { "..." }
    pub fn describe(&self) -> String { self.name + " says " + self.speak() }
}

class Dog : Animal {
    fn init(self, name: String) { super.init(name) }
    pub fn speak(&self) -> String { "woof" }
}

class Puppy : Dog {
    fn init(self, name: String) { super.init(name) }
    pub fn speak(&self) -> String { "yip (" + super.speak() + ")" }
}

fn announce(a: Animal) -> String { a.describe() }

fn main() -> i64 {
    io::println(announce(Animal("Generic")))
    io::println(announce(Dog("Rex")))
    io::println(announce(Puppy("Pip")))
    0
}
```

> [!NOTE]
> **Why the base method changes behaviour**
>
> `describe` is declared once on `Animal` and never overridden, yet it calls the *derived* `speak`. That is dynamic dispatch: the method is chosen from the instance's real type, not the static one.
