# std::cli

The command line, read the way the user wrote it. Say what the program takes; ask what it was given. `--help` prints a usage page built from the descriptions, and a mistake — an unknown option, a missing value, a positional that was not given — is named and, with `parseOrExit`, exits 2.

| Name | Signature | Does |
| --- | --- | --- |
| `Parser` | `class` | `Parser(program, summary)` |
| `Parser::flag` | `(&var self, name, short, help)` | `--name` / `-n`, present or not |
| `Parser::option` | `(&var self, name, short, help, fallback)` | takes a value; an empty fallback means no default |
| `Parser::optionMany` | `(&var self, name, short, help)` | repeatable; every value kept |
| `Parser::positional` | `(&var self, name, help)` | required, in declaration order |
| `Parser::rest` | `(&var self, name, help)` | any number of further positionals |
| `Parser::setVersion` | `(&var self, version)` | makes `--version` an option |
| `Parser::parse` | `(&self) -> Result<Arguments, Error>` | the program's own command line |
| `Parser::parseList` | `(&self, Vector<String>) -> Result<Arguments, Error>` | a list of one's own — for tests |
| `Parser::parseOrExit` | `(&self) -> Arguments` | prints help or the mistake, and exits |
| `Parser::usage` / `usageLine` | `(&self) -> String` | the help page, or its first line |
| `Arguments::has` | `(&self, name) -> bool` | was the flag given |
| `Arguments::value` / `intValue` | `(&self, name) -> String?` / `i64?` | the option's value, or its default |
| `Arguments::values` | `(&self, name) -> Vector<String>` | everything a repeatable option was given |
| `Arguments::positional` | `(&self, index) -> String?` | a declared positional |
| `Arguments::rest` | `(&self) -> Vector<String>` | whatever followed the declared ones |
| `Arguments::allPositionals` | `(&self) -> Vector<String>` | every positional, declared and rest alike |
| `Error` | `enum` | `Help`, `Version`, `UnknownOption`, `MissingValue`, `FlagGivenValue`, `MissingPositional`, `TooManyPositionals` |

**Declaring, parsing, and the help page**

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

    // `parse()` would read the real command line; a list stands in here.
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
    io::print(app.usage())
    0
}
```

| Written | Read as |
| --- | --- |
| `--name value`, `--name=value` | the option and its value |
| `-n value`, `-nvalue` | the same, by short name |
| `-lv` | two flags |
| `--` | everything after it is positional, however it looks |
| `-` | a positional — standard input, by convention |
| `--help`, `-h`, `--version` | handled for you |

*The spellings accepted*
