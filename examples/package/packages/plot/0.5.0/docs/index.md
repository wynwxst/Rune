# plot

Charts made of characters, for a terminal. `bar` draws one row per value,
`sparkline` one character per value, and `scatter` a grid of points.

```rune
import plot

let labels: [3:String] = ["mon", "tue", "wed"]
let sales: [3:f64] = [3.0, 7.0, 5.0]
io::print(plot::bar(labels, sales, 20))
io::println(plot::sparkline(sales))
```
