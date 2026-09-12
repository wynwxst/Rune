# std::cli

The command line, read the way the user wrote it. Say what the program
takes, then ask what it was given. `--help` prints a usage page built from
the descriptions, and a mistake is named and — with `parseOrExit` — exits 2.

## Declaring, and parsing

`parse()` reads the real command line. `parseList` takes a list of one's
own, which is what a test wants and what this page uses.

```rune
import std::io
import std::cli
import std::collections::vector

fn main() -> i64 {
    var app = cli::Parser("greet", "Prints a greeting.")
    app.flag("loud", "l", "shout it")
    app.option("name", "n", "who to greet", "world")
    app.optionMany("extra", "x", "more names")
    app.positional("times", "how many times")
    app.rest("files", "files to read afterwards")

    match app.parseList(vec!("-l", "--name=ada", "-x", "p", "-xq", "3", "a.txt")) {
        Ok(args) => {
            io::println(args.has("loud"))
            io::println(args.value("name") ?? "")
            io::println(args.values("extra").length())
            io::println(args.intValue("times") ?? 0)
            io::println(args.rest().length())
        },
        Err(e) => io::println(e),
    }
    match app.parseList(vec!("--bogus")) {
        Ok(args) => io::println("accepted?"),
        Err(e) => io::println(e),
    }
    match app.parseList(vec!("--help")) {
        Ok(args) => io::println("accepted?"),
        Err(cli::Error::Help) => io::print(app.usage()),
        Err(e) => io::println(e),
    }
    0
}
```

## In a real program

```rune
import std::io
import std::cli

fn main() -> i64 {
    var app = cli::Parser("count", "Counts to a number.")
    app.option("upto", "u", "where to stop", "3")
    app.flag("quiet", "q", "print only the total")
    let args = app.parseOrExit()          // --help and mistakes handled here
    let upto = args.intValue("upto") ?? 3
    for i in 1..=upto {
        if !args.has("quiet") { io::println(i) }
    }
    io::println("total " + upto.$str())
    0
}
```
