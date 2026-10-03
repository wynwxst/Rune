# `?` converts the error

A layered program has layered error types: the file layer fails with a `FileError`, the parser with a `ParseError`, and the application with something that wraps both. `?` hands the error out as the function's own error type, so when the two differ it looks for a way from one to the other — `bind Theirs into Ours`, the same as `bind As<Ours> to Theirs` — and calls its `convert` on the way out. That is the same `As` that `into` dispatches through, so writing the conversion once serves both. An error type that converts implicitly, a narrow integer into a wide one, is simply widened.

**Layered errors without a `mapErr` at every boundary**

```rune
import std::io

enum ParseError { Empty, NotANumber { text: String } }
enum AppError { Parse(ParseError), Disk(io::FileError) }

bind ParseError into AppError {
    fn convert(&self) -> AppError { AppError::Parse(self.$clone()) }
}
bind io::FileError into AppError {
    fn convert(&self) -> AppError { AppError::Disk(*self) }
}

fn parse(text: String) -> Result<i64, ParseError> {
    if text.$isEmpty() { return ParseError::Empty }
    match text.$toInt() {
        Some(n) => n,
        None => ParseError::NotANumber { text: text },
    }
}

fn run(text: String) -> Result<i64, AppError> {
    let n = parse(text)?                            // ParseError becomes AppError
    let contents = io::readToString("/no/such/file")?   // and so does FileError
    n + contents.$length()
}

fn describe(e: AppError) -> String {
    match e {
        AppError::Parse(ParseError::Empty) => "parse: empty",
        AppError::Parse(ParseError::NotANumber { text }) => "parse: not a number: " + text,
        AppError::Disk(f) => "disk: " + io::describe(f),
    }
}

fn main() -> i64 {
    match run("abc") { Ok(v) => io::println(v), Err(e) => io::println(describe(e)) }
    match run("42") { Ok(v) => io::println(v), Err(e) => io::println(describe(e)) }
    0
}
```

**Nothing says how to get from A to B**

```rune
enum A { X }
enum B { Y }

fn a() -> Result<i64, A> { A::X }
fn b() -> Result<i64, B> { let v = a()?; v }

fn main() -> i64 { 0 }
```

> [!NOTE]
> **Why the destination is the declared result**
>
> The conversion has to be written on the *function's* error type, which is what makes it safe: a `bind FileError into AppError` changes what `?` does only inside functions that return `Result<_, AppError>`, and says so where they are declared.
