# Using json

Everything the library does, in the order you meet it: reading a document,
looking inside one, building one, writing it back, and what happens when any
of that fails.

Every example on this page has been run.

- [Getting it](#getting-it)
- [Reading](#reading)
- [What a document is](#what-a-document-is)
- [Looking inside one](#looking-inside-one)
- [Building one](#building-one)
- [Writing](#writing)
- [Files](#files)
- [When it fails](#when-it-fails)
- [Walking a document](#walking-a-document)
- [What it refuses](#what-it-refuses)
- [Where it stops](#where-it-stops)

## Getting it

Name it in your `Rune.toml`:

```toml
[dependencies]
json = { path = "../json" }
```

and import it:

```rune
import json
```

That one import is enough for everything on this page. `json::value`,
`json::decode`, `json::encode`, `json::error` and `json::io` are there when
you want the parts directly, and the names worth having are re-exported under
`json` itself.

| Module | What is in it |
|---|---|
| `json` | `parse`, `write`, `pretty`, `load`, `save`, `object`, `array`, `null`, the `json!` macro, and the type names |
| `json::value` | `Value`, `Array`, `Object`, and every way of looking inside one |
| `json::decode` | text into a value, and `MAX_DEPTH` |
| `json::encode` | a value back into text |
| `json::error` | `Error`, `Kind`, and the position of a failure |
| `json::io` | the same two directions, over a file |

## Reading

`json::parse` takes the whole document and hands back a `Result`:

```rune
let doc = match json::parse("{\"name\": \"ada\", \"age\": 36}") {
    Ok(v) => v,
    Err(e) => {
        io::eprintln(e.describe())
        return 1
    },
}
```

In a function that already returns a `Result`, `?` says the same thing in one
character:

```rune
fn portOf(source: String) -> Result<i64, json::Error> {
    let doc = json::parse(source)?
    Ok(doc.get("port").or(json::null()).asInt().or(8080))
}
```

```
portOf("{\"port\": 9000}")   ->   9000
portOf("{}")                 ->   8080
```

The whole of the source has to be that one value. `1 2` is an error, not the
number one, because a document that continues is a document that was not
understood. When you *do* want to read one value and keep going —
several documents in one buffer — `json::decode::parsePrefix` is the form that
hands back where it stopped. There is an example of that
[in the examples](examples.md#a-stream-of-documents).

## What a document is

A parsed document is a `json::Value`, which is one of seven shapes:

```rune
pub enum Value {
    Null,
    Bool(bool),
    Int(i64),        // written with no `.` and no exponent
    Number(f64),     // everything else
    Text(String),
    Array(Array),
    Object(Object),
}
```

JSON has six; this has seven, because a whole number is worth keeping apart
from a fractional one. That is what makes `3` still `3` after a round trip
rather than `3.0`:

```rune
json::write(json::parse("3").unwrap())      // 3
json::write(json::parse("3.0").unwrap())    // 3.0
```

`Array` and `Object` are classes rather than payloads carried in the enum. A
value can contain a value, and an enum is a value type — a variant holding one
directly would have no finite size. The class is the indirection that makes
the shape possible, and it also means passing a large document around costs a
pointer rather than a copy.

An `Object` keeps **insertion order**, so a document read and written back
comes out looking like the one that went in.

## Looking inside one

Every accessor answers with an `Option`. A document is data from somewhere
else, so its shape is a question rather than an assumption.

| Call | Answers |
|---|---|
| `get(name)` | the member, when this is an object that has one |
| `index(n)` | the element, when this is an array that has one |
| `asText()` | the string inside |
| `asInt()` | the number inside, as an `i64` |
| `asFloat()` | the number inside, as an `f64` |
| `asBool()` | the boolean inside |
| `asArray()` / `asObject()` | the class inside, to walk it |
| `kind()` | `"null"`, `"boolean"`, `"number"`, `"string"`, `"array"`, `"object"` |
| `isNull()` | whether it is `null` |
| `length()` | elements or members; `0` for everything else |

```rune
doc.get("name").unwrap().asText().or("?")     // ada
doc.get("age").unwrap().asInt().or(0)         // 36
doc.kind()                                    // object
doc.length()                                  // 2
doc.get("nobody")                             // nothing
```

Reaching into the wrong shape gives nothing rather than aborting, so a chain
of these ends in `nil` instead of in a panic:

```rune
doc.get("user").unwrap().get("name").unwrap().asText()   // Some("ada")
doc.get("name").unwrap().get("x")                        // None — a string
                                                         // has no members
```

An accessor asks; it never assumes. A string is not a number, and `1.5` is not
an integer:

```rune
json::parse("1.5").unwrap().asInt()      // nothing — 1.5 is not an integer
json::parse("4.0").unwrap().asInt()      // Some(4) — this one is
```

Rounding `1.5` silently would be a worse answer than saying so.

## Building one

`json!` writes a document the way JSON is written, and is the form to reach
for when you know the shape:

```rune
let doc = json!{
    "name": "grace",
    "age": 45,
    "member": true,
    "score": 1.5,
    "tags": ["founder", "analyst"],
    "address": { "city": "London", "postcode": "N1" },
    "retired": null,
}
```

```
{"name":"grace","age":45,"member":true,"score":1.5,"tags":["founder","analyst"],"address":{"city":"London","postcode":"N1"},"retired":null}
```

A value is any expression, so a document can be built out of what the program
already has:

```rune
let name = "ada"
let year = 1815
json!{ "name": name, "next": year + 1 }     // {"name":"ada","next":1816}
```

[The `json!` page](macro.md) has the rest of it: the other delimiters, what
each rule matches, and what it expands to.

When the shape is only known as the program runs, build it by hand. This is
the one place that wants `import json::value` as well, because it names the
classes and the variants rather than only the `Value` they add up to:

```rune
import json
import json::value

var doc = value::Object()
doc.put("name", "grace" into json::Value)
doc.put("age", 45 into json::Value)

var tags = value::Array()
tags.push("x" into json::Value)
doc.put("tags", value::Value::Array(tags))

json::write(value::Value::Object(doc))
// {"name":"grace","age":45,"tags":["x"]}
```

`json::Value`, `json::Array`, `json::Object` and `json::Error` are type
aliases, so they are names for annotations — a parameter, a return type, an
`into` — and not paths to reach a constructor or a variant through.
`value::Object()` and `value::Value::Array(...)` are the spellings for those.

The two ways of building mix freely. A `json!` document can be edited
afterwards, and a value built by hand can be a member inside a `json!`:

```rune
let doc = json!{ "a": 1 }
match doc.asObject() {
    Some(o) => { var members = o; members.put("b", 2 into json::Value) },
    None => {},
}
json::write(doc)                              // {"a":1,"b":2}

json::write(json!{ "tags": value::Value::Array(tags) })
// {"tags":["x"]}
```

`into` rather than a constructor per type: `As` is the mark the language
already dispatches conversions through, so `7 into json::Value` works wherever
a `Value` is wanted. There are conversions from `i64`, `i32`, `f64`, `bool`
and `String`.

`withMember` and `withItem` are the same two operations written to chain. They
hand back the value they were given — an `Array` and an `Object` are classes,
so the value holds a reference to the one that was just changed:

```rune
json::object().withMember("a", 1 into json::Value)
              .withMember("b", json::array().withItem(2 into json::Value))
// {"a":1,"b":[2]}
```

That chain is exactly what `json!` expands to.

## Writing

```rune
json::write(doc)     // on one line
json::pretty(doc)    // one member or element per line, two-space indent
```

```
{"a":[1,2]}

{
  "a": [
    1,
    2
  ]
}
```

An empty array or object stays on one line, because there is nothing to lay
out. `Value` also binds `io::Display`, so a document can be printed directly:

```rune
io::println(doc)     // the compact form
```

## Files

```rune
json::load(path)          // Result<Value, Error>
json::save(path, doc)     // Error?, laid out — nil means it worked
```

A read that fails and a document that will not parse are both "this file did
not give me a value", so both come back as a `json::Error` and a caller that
only wants to report it does not have to match on two error types:

```rune
match json::load("/tmp/absent.json") {
    Ok(doc) => io::println(doc),
    Err(e) => io::eprintln(e.describe()),   // json: no such file
}
```

`json::io::write` is the compact counterpart of `save`.

## When it fails

Every failure carries the position it happened at, because an error without
one is almost useless on a document of any size.

```rune
match json::parse("{\"a\": 1,}") {
    Ok(doc) => {},
    Err(e) => {
        io::println(e.describe())
        io::println(e.line.$str() + ":" + e.column.$str())   // 1:9
        io::println(e.offset.$str())                          // 8
    },
}
```

```
json: unexpected character: an object may not end with a comma at line 1, column 9
```

`line` and `column` count from one, because that is what an editor shows;
`offset` is the byte, which is what a slice wants. `describe()` is the whole
sentence, and it is what `io::Display` gives too.

When a program has to tell one failure from another, match on `kind`:

| `error::Kind` | Means |
|---|---|
| `Ended` | the document stopped in the middle of something |
| `Unexpected` | a character that cannot start a value, or cannot be there |
| `BadNumber` | `01`, `.5`, `1.`, `1e` — a number the grammar does not allow |
| `BadString` | a string that never closed, or an escape that is not one |
| `Trailing` | something after the value the document was supposed to be |
| `TooDeep` | nested past `decode::MAX_DEPTH` |
| `File { cause }` | the file could not be read or written |

```rune
match e.kind {
    error::Kind::Ended => "the document stops early",
    error::Kind::File { cause } => "the file itself",
    _ => "something in the document",
}
```

## Walking a document

`Object::keys()` gives the member names in the order they were first set, and
`Array::length()` with `at` covers the other side. Together they walk anything:

```rune
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
```

```
doc.name = "ada"
doc.tags[0] = "founder"
doc.tags[1] = "analyst"
doc.address.city = "London"
doc.retired = null
```

Matching on the `Value` itself needs `import json::value`, because the variant
names live there. Everything else on this page works with `import json` alone.

## What it refuses

A parser is judged by what it refuses as much as by what it accepts. This one
follows the grammar exactly:

| Written | Why it is an error |
|---|---|
| `01` | a number may not have a leading zero |
| `.5` | a number needs a digit before the point |
| `1.` | a number needs a digit after the point |
| `1e` | an exponent needs digits |
| `[1, 2, ]` | a trailing comma |
| `{a: 1}` | a member name has to be a string |
| `"a` | a string that never closes |
| `"a\qb"` | an escape that is not one |
| `1 2` | more than one value |

```
json: malformed number: a number may not have a leading zero at line 1, column 2
json: unexpected character: '@' cannot start a value at line 1, column 1
json: expected ',' or '}' at line 3, column 14
```

Comments and trailing commas are JSON5, not JSON, and this library reads JSON.

## Where it stops

Nesting is capped at `json::decode::MAX_DEPTH`, which is 200. A document made
of nothing but `[` cannot use the parser's own recursion to exhaust the stack.

There is no streaming: the whole document is in memory, which is what
`readToString` hands over and what a configuration file or an HTTP body is
anyway. `parsePrefix` reads one value and says where it stopped, which covers
a buffer holding several documents, but not a single document too large to
hold.

There is no mapping to and from your own structs. That wants reflection over
field names, and this library stops at the value — `json!` writes a document
out, but it does not know what your `Person` is.
