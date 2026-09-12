# It really is compile time

None of this survives to run time. The whole of `main` below folds to one constant, and a `match` on `kindOf` keeps only the arm its type reaches:

```sh
fn main() -> i64 {
    reflect::sizeOf<Point>() as i64 + reflect::alignOf<Point>() as i64 +
        offset_of!(Point, y) as i64 + reflect::typeId<Point>() as i64 % 7
}

$ runec -O1 --emit-llvm -o - main.rune
define i64 @main() {
entry:
  ret i64 27
}
```

That is what makes `conforms` worth having: a generic function can take a better path for types that offer one without demanding it of every type, and the path not taken is never emitted.

**Specialising on what a type offers**

```rune
import std::reflect
import std::io

struct Named { n: i64 }
bind io::Display to Named {
    fn display(&self) -> String { "Named#" + self.n.$str() }
}

struct Plain { n: i64 }

/// One body, two instantiations, each keeping only its own branch.
fn render<T>(value: T) -> String {
    if reflect::conforms<T, io::Display>() { return "displayable" }
    reflect::describe(value)
}

fn main() -> i64 {
    println!("{}", render(Named { n: 1 }))
    println!("{}", render(Plain { n: 2 }))
    0
}
```
