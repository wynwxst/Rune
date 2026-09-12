# The three variant shapes

**One type, several shapes**

```rune
import std::io

// An enum is how a value that is one of several shapes gets a single type.
enum Json {
    Null,
    Bool(bool),
    Number(f64),
    Text(String),
}

fn render(v: Json) -> String {
    match v {
        Json::Null => "null",
        Json::Bool(b) => b.$str(),
        Json::Number(n) => n.$str(),
        Json::Text(t) => "'" + t + "'",
    }
}

fn main() -> i64 {
    io::println(render(Json::Null))
    io::println(render(Json::Bool(true)))
    io::println(render(Json::Number(2.5)))
    io::println(render(Json::Text("hi")))
    0
}
```

**Unit, tuple and struct variants**

```rune
import std::io

enum Shape {
    Empty,                              // unit: no payload
    Circle(f64),                        // tuple: positional payload
    Rect { width: f64, height: f64 },   // struct: named payload
}

fn area(s: Shape) -> f64 {
    match s {
        Shape::Empty => 0.0,
        Shape::Circle(r) => 3.14159265 * r * r,
        Shape::Rect { width, height } => width * height,
    }
}

fn main() -> i64 {
    io::println(area(Shape::Empty))
    io::println(area(Shape::Circle(2.0)))
    io::println(area(Shape::Rect { width: 3.0, height: 4.0 }))
    0
}
```
