# std::reflect

What the compiler already knows about a type, asked at compile time: its
name, kind, size and layout, whether it conforms to a mark, and whether it
may cross a thread. Every answer is a constant in the generated code.

## Asking about a type

```rune
import std::io
import std::reflect

struct Point { pub x: f64, pub y: f64 }
enum Colour { Red, Green }

fn kindName<T>() -> String {
    match reflect::kindOf<T>() {
        reflect::Kind::Int => "int",
        reflect::Kind::Float => "float",
        reflect::Kind::String => "string",
        reflect::Kind::Struct => "struct",
        reflect::Kind::Enum => "enum",
        reflect::Kind::Class => "class",
        _ => "something else",
    }
}

fn main() -> i64 {
    io::println(reflect::typeName<Point>())
    io::println(kindName<i64>() + " " + kindName<Point>() + " " + kindName<Colour>())
    io::println(reflect::sizeOf<Point>())
    io::println(reflect::fieldCount<Point>())
    io::println(reflect::fieldName<Point>(1))
    io::println(reflect::fieldType<Point>(0))
    io::println(reflect::offsetOf<Point>("y"))
    io::println(reflect::conforms<i64, io::Display>())
    io::println(reflect::isSend<String>())
    io::println(reflect::describe(Point { x: 1.0, y: 2.0 }))
    0
}
```
