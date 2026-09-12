# stats

Summary statistics over a slice of numbers. Every question about an empty
slice answers `nil`, because there is no mean of nothing.

```rune
import stats

let scores: [5:f64] = [2.0, 4.0, 4.0, 4.0, 6.0]
io::println(stats::mean(scores) ?? 0.0)        // 4.0
io::println(stats::stddev(scores) ?? 0.0)      // 1.264...
```
