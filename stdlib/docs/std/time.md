# std::time

A length of time, a clock to measure one against, and the calendar. `Time`
is a duration to the nanosecond; `Instant` is a reading from a clock that
only goes forwards; `Date`, `TimeOfDay` and `DateTime` are for saying *when*.

## Durations, and measuring

A duration is built by naming its unit, because a bare number is how the
wrong unit gets passed.

```rune
import std::io
import std::time
import std::thread

fn main() -> i64 {
    let pause = time::milliseconds(20)
    io::println(pause)
    io::println((pause + time::seconds(1)).asMilliseconds())
    io::println(time::hours(2).asSeconds())

    let started = time::now()
    thread::sleep(pause)
    let taken = started.elapsed()
    io::println(taken >= pause)
    io::println(taken.asMilliseconds() < 5000)
    0
}
```

## Dates

Day arithmetic never has to think about leap years twice: it is done on a
count of days since 1970-01-01.

```rune
import std::io
import std::time

fn main() -> i64 {
    let d = time::date(2026, 9, 11) ?? time::dateFromDays(0)
    io::println(d)
    io::println(d.weekday())
    io::println(d.plusDays(30))
    io::println(d.plusMonths(5))
    io::println((time::date(2024, 1, 31) ?? d).plusMonths(1))   // clamped
    io::println(d.dayOfYear())
    io::println(d.next(time::Weekday::Monday))
    io::println(d.daysUntil(d.plusDays(10)))
    io::println(time::date(2023, 2, 29).isNil())
    io::println(time::parseDate("2026-01-05") ?? d)
    io::println(d < d.plusDays(1))
    0
}
```

## Moments

A `DateTime` carries its offset from UTC. `utcNow` and `localNow` read the
wall clock; `iso` writes RFC 3339 and `parseDateTime` reads it back.

```rune
import std::io
import std::time

fn main() -> i64 {
    let moment = time::parseDateTime("2026-09-11T10:20:30Z") ?? time::fromUnix(0)
    io::println(moment)
    io::println(moment.toUnix())
    io::println(moment.plus(time::hours(25)))
    io::println(moment.toOffset(3600))
    io::println(time::fromUnix(1700000000))
    io::println(time::fromUnix(1700000000).date.weekday())

    let now = time::utcNow()
    io::println(now.date.year >= 2026)
    let local = time::localNow()
    io::println(local.inUtc().toUnix() - now.toUnix() < 5)
    io::println(time::today() == local.date)
    0
}
```
