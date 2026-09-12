# json

Reading and writing JSON, in about 1,200 lines of Rune.

```rune
import json

let doc = json::parse("{\"name\": \"ada\", \"age\": 36}")?

doc.get("name").unwrap().asText()      // Some("ada")
doc.get("age").unwrap().asInt()        // Some(36)

json::write(doc)                       // {"name":"ada","age":36}
json::pretty(doc)                      // the same, laid out
```

## Documentation

| | |
|---|---|
| [Using json](docs/guide/usage.md) | Everything the library does, in the order you meet it |
| [The json! macro](docs/guide/macro.md) | Writing a document out, and what it expands to |
| [Examples](docs/guide/examples.md) | Whole programs, each one compiled and run |

The rest of `docs/` is the reference, generated from the source by `rune doc`:
one folder per module or type, and `docs/target/index.html` is the whole of it
in one page.

There is also a small tool over it, which is the quickest way to see the two
halves agree with each other:

```bash
rune run -- document.json                 # laid out
rune run -- --compact document.json       # on one line
rune run -- --get address.city document.json
rune run -- --get 'tags[1]' document.json
echo '{"a":1}' | rune run --               # from standard input
```

## What is where

| File | Contents |
|------|----------|
| `src/lib.rune` | The library root: `parse`, `write`, `pretty`, `load`, `save`, and the `json!` macro. |
| `src/value.rune` | `Value`, `Array`, `Object`, and every way of looking inside one. |
| `src/decode.rune` | Text into a value. |
| `src/encode.rune` | A value back into text, compact or laid out. |
| `src/error.rune` | What went wrong, and where. |
| `src/io.rune` | The same two directions, over a file. |
| `src/main.rune` | The tool. |

## The value

JSON has six shapes; `Value` has seven, because a whole number is worth
keeping apart from a fractional one:

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

That is what makes `3` still `3` after a round trip rather than `3.0`, and it
is why `asInt()` can be exact. `asInt()` on `4.0` gives `4`; on `1.5` it gives
nothing, because 1.5 is not an integer and rounding it silently would be a
worse answer than saying so.

`Array` and `Object` are classes rather than payloads carried in the enum. A
value can contain a value, and an enum is a value type — a variant holding one
directly would have no finite size. The class is the indirection that makes
the shape possible, and it also means passing a large document around costs a
pointer.

An `Object` keeps **insertion order**, so a document read and written back
comes out looking like the one that went in. A hash says where a member is;
the order says how it was written, and both are worth having.

## Looking inside one

Every accessor answers with an `Option`. A document is data from somewhere
else, so its shape is a question rather than an assumption:

```rune
doc.get("user").unwrap().get("name").unwrap().asText()   // Some("ada")
doc.get("nobody")                                        // None
doc.get("name").unwrap().get("x")                        // None — not an object
doc.get("tags").unwrap().index(1)                        // Some(…)
doc.kind()                                               // "object"
```

Reaching into the wrong shape gives nothing rather than aborting, so a chain
of these ends in `nil` instead of in a panic.

## Building one

`json!` writes a document the way JSON is written:

```rune
let doc = json!{
    "name": "grace",
    "age": 45,
    "member": true,
    "tags": ["founder", "analyst"],
    "address": { "city": "London" },
    "retired": null,
}

json::write(doc)
// {"name":"grace","age":45,"member":true,"tags":[...],"address":{...},"retired":null}
```

The braces of the invocation are the object's own, which is why the common
case reads as JSON does. A macro is handed the tokens *inside* the call and
not the ones around it, so everything nested keeps its own delimiters: `{ }`
is an object, `[ ]` an array, `null` is null, and anything else is an
expression converted with `into`.

```rune
json!["a", "b"]        // an array at the top level
json!(42)              // a number on its own
json!{}                // an empty object
json!([])              // an empty array — the outer delimiter is gone by the
                       // time the rules are tried, so this one is written out
```

A value is any expression, so a document can be built out of what the program
already has:

```rune
let name = "ada"
json!{ "name": name, "next": year + 1 }
```

It is `pub macro json` in `src/lib.rune`, and a `pub macro` reaches every
package that builds against this one — a `.rul` carries its modules' source,
so the macros come with it.

By hand, when the shape is not known until it runs:

```rune
var doc = json::Object()
doc.put("name", "grace" into json::Value)
doc.put("age", 45 into json::Value)

var tags = json::Array()
tags.push("x" into json::Value)
doc.put("tags", json::Value::Array(tags))

json::write(json::Value::Object(doc))
// {"name":"grace","age":45,"tags":["x"]}
```

`into` rather than a constructor per type: `As` is the mark the language
already dispatches conversions through, so `7 into json::Value` works wherever
a `Value` is wanted. `withMember` and `withItem` are the same two operations
written to chain — they are what `json!` expands to, and they hand back the
value they were given because an `Array` and an `Object` are classes.

## What it refuses, and where

A parser is judged by what it refuses as much as by what it accepts. This one
follows the grammar exactly — a leading zero, a bare `.5`, a trailing `1.`, an
empty exponent, a trailing comma and an unquoted member name are all errors —
and every failure carries the position it happened at:

```
json: expected ',' or '}' at line 3, column 14
json: malformed number: a number may not have a leading zero at line 1, column 2
json: unexpected character: '@' cannot start a value at line 1, column 1
```

Nesting is capped at `decode::MAX_DEPTH` (200), so a document made of nothing
but `[` cannot use the parser's own recursion to exhaust the stack.

`tests/basics.rune` has 93 checks, half of them documents that should not
parse and the line and column each failure should be reported at. The parser
also agrees with Python's `json` on 50 accept/reject cases and round-trips a
500-object document byte for byte.

## What it does not do

No streaming: the whole document is in memory, which is what `readAll` hands
over and what a configuration file or an HTTP body is anyway. No comments and
no trailing commas — those are JSON5, not JSON. No mapping to and from your own structs; that wants
reflection over field names, and this library stops at the value — `json!`
writes a document out, but it does not know what your `Person` is.
