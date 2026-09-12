# report

A text report assembled a piece at a time — sections, lines, statistics
with a sparkline histogram, lengths and temperatures in sensible units, bar
charts — then rendered for a terminal.

```rune
import report

var r = report::report("Weekly")
r.section("Sales")
r.statistic("units sold", sales)
r.distance("route", units::kilometres(12.5))
io::print(r.render())
```
