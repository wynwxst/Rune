# geometry

Points and vectors on the plane. A `Point` is a position; a `Vector` is what
one point is from another, and `b - a` builds one.

```rune
import geometry

let a = geometry::point(0.0, 0.0)
let b = geometry::point(3.0, 4.0)
io::println(geometry::distance(a, b))      // 5.0
io::println((b - a).unit())                // <0.6, 0.8>
```
