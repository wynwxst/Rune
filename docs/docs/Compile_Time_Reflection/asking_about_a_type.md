# Asking about a type

**What the compiler knows about a type**

```rune
import std::reflect

struct Point { x: f64, y: i32 }

fn main() -> i64 {
    println!("{} {}", reflect::typeName<Point>(), reflect::typeName<[3:f64]>())
    println!("size={} align={} stride={}",
             reflect::sizeOf<Point>(), reflect::alignOf<Point>(),
             reflect::strideOf<Point>())

    // Fields, by position: name, type, and where it begins.
    println!("{} fields", reflect::fieldCount<Point>())
    println!("{}: {} at {}", reflect::fieldName<Point>(0),
             reflect::fieldType<Point>(0), offset_of!(Point, x))
    println!("{}: {} at {}", reflect::fieldName<Point>(1),
             reflect::fieldType<Point>(1), offset_of!(Point, y))

    // An identity to compare, derived from the name.
    println!("{}", reflect::typeId<i64>() == reflect::typeId<i64>())
    0
}
```

| Asked | Answers |
| --- | --- |
| `typeName<T>()` | the fully qualified name, as `Any` reports it |
| `typeId<T>()` | a number equal for the same type, derived from the name |
| `kindOf<T>()` | which shape it is, as a `reflect::Kind` |
| `sizeOf<T>()` | bytes one value occupies |
| `alignOf<T>()` | the alignment it requires |
| `strideOf<T>()` | the distance between array elements |
| `offset_of!(T, f)` | where field `f` begins, in bytes |
| `fieldCount<T>()` | struct fields, tuple elements, enum variants |
| `fieldName<T>(i)` | the name of part *i* |
| `fieldType<T>(i)` | the name of its type |
| `conforms<T, M>()` | whether `T` binds the mark `M` |

> [!NOTE]
> **Why one of them is a macro**
>
> `offset_of!` is a macro because a field is a *name*, not a value: there is nothing to pass. It is written as a rule in `std::reflect` — `stringify!` carries the name through to an intrinsic that resolves it. Nothing about it is built in.
