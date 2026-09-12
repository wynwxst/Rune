# std::time

| Name | Signature | Does |
| --- | --- | --- |
| `Time` | `struct` | a length of time, to the nanosecond |
| `nanoseconds` … `hours` | `(count: i64) -> Time` | build one by naming its unit |
| `zero` | `() -> Time` | no time at all |
| `Time::asMilliseconds` and friends | `(&self) -> i64` | the whole duration in that unit; also `asSecondsFloat` |
| `Time::plus` / `minus` / `times` |  | also `+` and `-` |
| `Instant` | `struct` | a reading from a forward-only clock |
| `now` | `() -> Instant` | the clock, now |
| `Instant::since` / `elapsed` | `-> Time` | how long between two readings |

*`Time` is one `i64`, so it is `Send` and `Sync` for the same reason an `i64` is.*

The calendar is the other half: dates, times of day, and moments on the wall clock, in the proleptic Gregorian calendar with the arithmetic done on a count of days since 1970-01-01. `utcNow` and `localNow` read the wall clock — which, unlike `now`, can be set and can jump, so it is for saying *when* rather than for measuring.

| Name | Signature | Does |
| --- | --- | --- |
| `Date` | `struct { year, month, day }` | a calendar date; `month` and `day` count from one |
| `date` | `(year, month, day) -> Date?` | the date, or `nil` for a day that does not exist |
| `dateFromDays` | `(days: i64) -> Date` | days since 1970-01-01, negative before it |
| `Date::toDays` | `(&self) -> i64` | the inverse |
| `Date::weekday` | `(&self) -> Weekday` | `Monday` … `Sunday`, an enum that prints as its name |
| `Date::plusDays` / `plusMonths` / `plusYears` | `(&self, i64) -> Date` | arithmetic; a month lands clamped to its last day |
| `Date::daysUntil` | `(&self, other: Date) -> i64` | signed distance in days |
| `Date::dayOfYear` / `isLeapYear` / `next` |  | the position in the year; the year's shape; the next given weekday |
| `Date::iso` | `(&self) -> String` | `2026-09-11`; also what it prints as, and `<`, `==` compare dates |
| `parseDate` | `(String) -> Date?` | `2026-09-11` read back |
| `isLeapYear` / `daysInMonth` | `(year) -> bool` / `(year, month) -> i64` | the calendar's two facts |
| `TimeOfDay` | `struct { hour, minute, second, nanosecond }` | a time of day; `timeOfDay(h, m, s)` checks one |
| `DateTime` | `struct { date, time, offset }` | a moment: a date and time, `offset` seconds east of UTC |
| `utcNow` / `localNow` | `() -> DateTime` | the wall clock, in UTC or this machine's zone |
| `today` | `() -> Date` | today's date in this machine's zone |
| `fromUnix` / `fromUnixNanos` | `(i64) -> DateTime` | seconds or nanoseconds since the epoch, in UTC |
| `DateTime::toUnix` / `toUnixNanos` | `(&self) -> i64` | the inverse, whatever zone it is written in |
| `DateTime::inUtc` / `inLocalZone` / `toOffset` | `-> DateTime` | the same moment, written in another zone |
| `DateTime::plus` / `minus` / `since` |  | arithmetic with a `Time`; `<` and `==` compare moments |
| `DateTime::iso` | `(&self) -> String` | `2026-09-11T10:20:30Z`, or `+01:00` in a zone — RFC 3339 |
| `parseDateTime` | `(String) -> DateTime?` | the same read back; a bare date is midnight UTC |
| `localZoneName` | `() -> String` | `BST`, `PDT` — whatever the platform calls it right now |

*Dates compare and print; a `DateTime` in one zone equals the same moment in another.*

**Dates and moments**

```rune
import std::io
import std::time

fn main() -> i64 {
    let launch = time::date(2026, 9, 11) ?? time::dateFromDays(0)
    io::println(launch.weekday())
    io::println(launch.plusDays(30))
    io::println(launch.next(time::Weekday::Monday))

    let moment = time::parseDateTime("2026-09-11T10:20:30Z") ?? time::fromUnix(0)
    io::println(moment.plus(time::hours(25)))
    io::println(moment.toOffset(3600))
    io::println(time::fromUnix(1700000000))
    0
}
```
