# shapes

Circles, rectangles and polygons, each a `Shape`: `area`, `perimeter`,
`contains` and `bounds`. Built on `geometry`, which is where the points
come from.

```rune
import shapes
import geometry

let c = shapes::circle(geometry::origin(), 2.0)
io::println(c.area())
io::println(c.contains(geometry::point(1.0, 1.0)))
```
