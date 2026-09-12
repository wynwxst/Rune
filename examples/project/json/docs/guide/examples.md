# Examples

Whole programs, each one compiled and run. Every output below is what the
program actually printed.

- [Reading a configuration file](#reading-a-configuration-file)
- [Following a path](#following-a-path)
- [Building a payload](#building-a-payload)
- [Editing a document](#editing-a-document)
- [Reporting a failure](#reporting-a-failure)
- [Every leaf in a document](#every-leaf-in-a-document)
- [A stream of documents](#a-stream-of-documents)

## Reading a configuration file

A setting the document does not mention falls back to a default, and a file
that is not there is not an error worth stopping for.

```rune
import std::io
import json

/// One setting, or `fallback` when the document does not say.
fn textOr(doc: json::Value, name: String, fallback: String) -> String {
    doc.get(name).or(json::null()).asText().or(fallback)
}

fn intOr(doc: json::Value, name: String, fallback: i64) -> i64 {
    doc.get(name).or(json::null()).asInt().or(fallback)
}

fn main() -> i64 {
    let doc = match json::load("config.json") {
        Ok(v) => v,
        Err(e) => {
            io::eprintln(e.describe())
            json::object()
        },
    }

    io::println("host    " + textOr(doc, "host", "127.0.0.1"))
    io::println("port    " + intOr(doc, "port", 8080).$str())
    io::println("workers " + intOr(doc, "workers", 4).$str())
    0
}
```

With `{"host": "0.0.0.0", "workers": 8}` in the file:

```
host    0.0.0.0
port    8080
workers 8
```

`json::object()` as the fallback document is what makes the rest of `main`
straight-line code: an empty object has no members, so every setting takes its
default without a second path through the function.

## Following a path

`get` reaches one level. A dotted path is a loop around it, and a step into
the wrong shape ends the walk rather than aborting.

```rune
import std::io
import json

/// The value at `path`, or nothing.
fn at(doc: json::Value, path: String) -> json::Value? {
    var here = doc
    var name = ""
    var i = 0
    while i <= path.$length() {
        let end = i == path.$length()
        let c = if end { "." } else { path.$substring(i, i + 1) }
        if c == "." {
            if !name.$isEmpty() {
                match here.get(name) {
                    Some(next) => here = next,
                    None => return nil,
                }
                name = ""
            }
        } else {
            name += c
        }
        i += 1
    }
    here
}

fn main() -> i64 {
    let source = "{\"user\": {\"name\": \"ada\", \"roles\": [\"admin\"]}}"
    let doc = json::parse(source).unwrap()

    match at(doc, "user.name") {
        Some(v) => io::println("user.name = " + json::write(v)),
        None => io::println("no such member"),
    }
    match at(doc, "user.email") {
        Some(v) => io::println("user.email = " + json::write(v)),
        None => io::println("user.email is not there"),
    }
    match at(doc, "user.name.first") {
        Some(v) => io::println("reached into a string?"),
        None => io::println("a string has no members"),
    }
    0
}
```

```
user.name = "ada"
user.email is not there
a string has no members
```

`src/main.rune` has the fuller version of this, which also understands `[0]`
for an index — it is what `rune run -- --get user.name doc.json` uses.

## Building a payload

```rune
import std::io
import json

fn main() -> i64 {
    let user = "ada"
    let attempt = 2

    let payload = json!{
        "user": user,
        "attempt": attempt,
        "scopes": ["read", "write"],
        "client": { "name": "rune-json", "version": "0.1.0" },
        "note": null,
    }

    io::println(json::write(payload))
    io::println(json::pretty(payload))
    0
}
```

```
{"user":"ada","attempt":2,"scopes":["read","write"],"client":{"name":"rune-json","version":"0.1.0"},"note":null}
{
  "user": "ada",
  "attempt": 2,
  "scopes": [
    "read",
    "write"
  ],
  "client": {
    "name": "rune-json",
    "version": "0.1.0"
  },
  "note": null
}
```

## Editing a document

Setting a member that is already there replaces the value and leaves it where
it was, so an edit does not reshuffle a document. Removing one takes it out of
the order too.

```rune
import std::io
import json
import json::value

fn redact(doc: json::Value, name: String) -> json::Value {
    match doc.asObject() {
        Some(o) => {
            var members = o
            if members.holds(name) {
                members.put(name, "***" into json::Value)
            }
        },
        None => {},
    }
    doc
}

fn main() -> i64 {
    let doc = json::parse(
        "{\"user\":\"ada\",\"password\":\"hunter2\",\"tries\":3}").unwrap()

    io::println(json::write(redact(doc, "password")))

    match doc.asObject() {
        Some(o) => { var members = o; members.remove("tries") },
        None => nil,
    }
    io::println(json::write(doc))
    0
}
```

```
{"user":"ada","password":"***","tries":3}
{"user":"ada","password":"***"}
```

`redact` hands the same document back rather than a copy: an `Object` is a
class, and the `Value` holds a reference to it.

## Reporting a failure

What went wrong, where, and which kind of wrong it was.

```rune
import std::io
import json
import json::error

fn report(source: String) {
    match json::parse(source) {
        Ok(doc) => io::println("ok: " + json::write(doc)),
        Err(e) => {
            io::println(e.describe())
            io::println("  at offset " + e.offset.$str() +
                        ", line " + e.line.$str() +
                        ", column " + e.column.$str())
            let kind = match e.kind {
                error::Kind::Ended => "the document stops early",
                error::Kind::Unexpected => "a character that cannot be there",
                error::Kind::BadNumber => "a number the grammar refuses",
                error::Kind::BadString => "a string the grammar refuses",
                error::Kind::Trailing => "more than one value",
                error::Kind::TooDeep => "nested too deeply",
                error::Kind::File { cause } => "the file itself",
            }
            io::println("  kind: " + kind)
        },
    }
}

fn main() -> i64 {
    report("{\"a\": 1}")
    report("{\"a\": 1,}")
    report("[1, 2")
    report("01")
    report("1 2")
    0
}
```

```
ok: {"a":1}
json: unexpected character: an object may not end with a comma at line 1, column 9
  at offset 8, line 1, column 9
  kind: a character that cannot be there
json: the document ended too soon: expected ',' or ']' at line 1, column 6
  at offset 5, line 1, column 6
  kind: the document stops early
json: malformed number: a number may not have a leading zero at line 1, column 2
  at offset 1, line 1, column 2
  kind: a number the grammar refuses
json: trailing content after the value at line 1, column 3
  at offset 2, line 1, column 3
  kind: more than one value
```

## Every leaf in a document

`keys()` gives the member names in the order they were first set, so a
recursive walk comes out in document order.

```rune
import std::io
import json
import json::value

fn walk(path: String, doc: json::Value) {
    match doc {
        value::Value::Object(o) => {
            for name in o.keys() {
                walk(path + "." + name, o.at(name).unwrap())
            }
        },
        value::Value::Array(a) => {
            var i = 0
            while i < a.length() {
                walk(path + "[" + i.$str() + "]", a.at(i).unwrap())
                i += 1
            }
        },
        _ => io::println(path + " = " + json::write(doc)),
    }
}

fn main() -> i64 {
    let doc = json!{
        "name": "ada",
        "tags": ["founder", "analyst"],
        "address": { "city": "London" },
        "retired": null,
    }
    walk("doc", doc)
    0
}
```

```
doc.name = "ada"
doc.tags[0] = "founder"
doc.tags[1] = "analyst"
doc.address.city = "London"
doc.retired = null
```

`null` is a leaf like any other, and `o.at(name)` finds it — a member that is
present and empty is not the same as a member that is not there.

## A stream of documents

`parse` insists the whole source is one value. `parsePrefix` reads one and
hands back where it stopped, which is what a buffer holding several documents
needs.

```rune
import std::io
import json
import json::decode

fn main() -> i64 {
    let stream = "{\"n\":1}\n{\"n\":2}\n{\"n\":3}\n"
    var at = 0
    var total = 0
    while at < stream.$length() {
        match decode::parsePrefix(stream, at) {
            Ok(pair) => {
                total += pair.0.get("n").unwrap().asInt().or(0)
                at = pair.1
            },
            // The tail is whitespace once the last document is read.
            Err(e) => break,
        }
    }
    io::println("total " + total.$str())
    0
}
```

```
total 6
```

The pair is `(value, offset)`. Leaving the loop on the first failure is right
here because the only thing left after the last document is the whitespace
that ends the buffer; a reader that has to tell "done" from "malformed" should
skip whitespace itself and check `at` against the length before parsing.
