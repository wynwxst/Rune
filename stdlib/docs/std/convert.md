# std::convert

Conversions the program supplies, as opposed to the ones the compiler already
knows. `as` is the compiler's and covers numbers, enums and pointers with
fixed meanings. `into` is yours: it dispatches through the `As<Target>`
mark, and so does `?` when a function's error type differs from the one it
is handed.

## Writing one

`bind Source into Target` is the spelling — the same as `bind As<Target> to
Source`. A type may convert into as many others as it likes.

```rune
import std::io
import std::convert

struct Celsius { pub degrees: f64 }
struct Fahrenheit { pub degrees: f64 }
struct Kelvin { pub degrees: f64 }

bind Celsius into Fahrenheit {
    fn convert(&self) -> Fahrenheit { Fahrenheit { degrees: self.degrees * 9.0 / 5.0 + 32.0 } }
}
bind Celsius into Kelvin {
    fn convert(&self) -> Kelvin { Kelvin { degrees: self.degrees + 273.15 } }
}

/// Anything that can become a Fahrenheit reading.
fn report<T: convert::As<Fahrenheit>>(reading: T) {
    io::println((reading into Fahrenheit).degrees)
}

fn main() -> i64 {
    let c = Celsius { degrees: 100.0 }
    io::println((c into Fahrenheit).degrees)
    io::println((c into Kelvin).degrees)
    report(c)
    0
}
```

## Implicitly, where the destination is written down

A conversion the program defines is put in on its own wherever the type
wanted is written down — an argument, an annotated binding, a field, a
declared result, an assignment — so `(c into Fahrenheit)` is only needed
where nothing says what is wanted.

The source may be any type, a tuple or a **pointer** among them, and a bind
may be generic. One binding lets every typed pointer go where C asks for
bytes:

```rune
import std::io
import std::mem

bind<T> *var T into *var u8 {
    fn convert(&self) -> *var u8 { unsafe { self as *var u8 } }
}

extern "C" {
    fn realloc(ptr: *var u8, size: usize) -> *var u8
    fn free(ptr: *var u8)
}

#unsafe
fn main() -> i64 {
    var items = 0 as *var i64
    items = realloc(items, mem::size_of<i64>() * 4) as *var i64
    items[0] = 7
    io::println(items[0])
    free(items)
    0
}
```

In a binding on a pointer type, `self` is the pointer itself. Importing a
file or a library that declares such a binding brings it along.

## What `?` does with it

```rune
import std::io

enum DiskError { Missing }
enum AppError { Disk(DiskError), Other(String) }

bind DiskError into AppError {
    fn convert(&self) -> AppError { AppError::Disk(*self) }
}

fn read() -> Result<String, DiskError> { DiskError::Missing }

fn run() -> Result<String, AppError> {
    let text = read()?                 // DiskError becomes AppError here
    text + "!"
}

fn main() -> i64 {
    match run() {
        Ok(t) => io::println(t),
        Err(AppError::Disk(DiskError::Missing)) => io::println("the disk error, converted"),
        Err(AppError::Other(m)) => io::println(m),
    }
    0
}
```
