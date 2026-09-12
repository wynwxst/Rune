# units

A length that knows what it is measured in. Build one by naming the unit,
read it out in any other, add and compare freely: the unit is carried, so
the arithmetic is always in metres underneath.

```rune
import units

let run = units::kilometres(5.0) + units::metres(200.0)
io::println(run)                 // 5.2 km
io::println(run.asMiles())
```
