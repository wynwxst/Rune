# The json! macro

`json!` writes a document the way JSON is written:

```rune
let doc = json!{
    "name": "ada",
    "age": 36,
    "member": true,
    "tags": ["founder", "analyst"],
    "address": { "city": "London" },
    "retired": null,
}
```

It is a declarative macro — a rewrite from one run of tokens to another,
chosen by pattern — so all of this happens before anything is parsed as an
expression. What it stands for is an ordinary chain of calls.

- [Why the braces work](#why-the-braces-work)
- [The forms](#the-forms)
- [Values are expressions](#values-are-expressions)
- [What it expands to](#what-it-expands-to)
- [When something is wrong](#when-something-is-wrong)
- [Where it is visible](#where-it-is-visible)
- [When not to use it](#when-not-to-use-it)

## Why the braces work

A macro may be invoked with any of the three delimiters — `json!( )`,
`json![ ]`, `json!{ }` — and is handed the tokens *inside* the call, not the
ones around it. `json!{ "a": 1 }` therefore arrives as

```
"a" : 1
```

with the braces already gone. That is what lets the common case read as JSON
does: the invocation's own braces are the object's.

Everything nested keeps its delimiters, because those are inside the call:

```rune
json!{ "address": { "city": "London" } }
//                 └── this pair is part of what the macro sees
```

So `{ ... }` inside is an object, `[ ... ]` inside is an array, and the rules
below can tell them apart.

## The forms

Rules are tried in order; the first that fits wins.

| Written | Matches | Gives |
|---|---|---|
| `{ "a": 1 }` | a braced run of `name: value` pairs | an object |
| `[ 1, 2 ]` | a bracketed run of values | an array |
| `null` | the word | `json::null()` |
| `"a": 1, "b": 2` | bare pairs — the invocation's own braces | an object |
| `1, 2, 3` | a bare comma-separated run | an array |
| *(nothing)* | `json!{}` | an empty object |
| anything else | one expression | `expr into json::Value` |

Which means, at the top level:

```rune
json!{ "a": 1 }        // {"a":1}      an object
json!["a", "b", 3]     // ["a","b",3]  an array
json!(42)              // 42           a number
json!("x")             // "x"          a string
json!(null)            // null
json!{}                // {}           an empty object
json!([])              // []           an empty array
json![7]               // 7            one element and no comma is a value
```

Two of those are worth a second look.

**`json![7]` is `7`, not `[7]`.** The invocation's brackets are gone by the
time the rules are tried, so `7` is indistinguishable from `json!(7)`. The
array rule needs a comma to fire. Write `json!([7])` for a one-element array —
the inner brackets are part of what the macro sees.

**`json!{}` is an empty object and `json![]` would be one too**, for the same
reason: neither has any tokens left to tell them apart. An empty array is
written `json!([])`.

Trailing commas are fine, and are the reason the multi-line form above ends
with one:

```rune
json!{ "a": 1, }       // {"a":1}
```

## Values are expressions

A value is not restricted to a literal. Anything that converts into a
`json::Value` will do, so a document can be built out of what the program
already has:

```rune
let name = "ada"
let year = 1815

json!{ "name": name, "next": year + 1 }
// {"name":"ada","next":1816}
```

The conversion is `into`, so the value's type needs `As<json::Value>`. There
are conversions from `i64`, `i32`, `f64`, `bool` and `String` — and from
`json::Value` itself, so a document you already have goes in beside the
literals:

```rune
json!{ "tags": value::Value::Array(tags) }    // {"tags":["x"]}
```

You can bind your own for anything else:

```rune
struct Duration { seconds: i64 = 0 }

bind As<json::Value> to Duration {
    fn convert(&self) -> json::Value { self.seconds into json::Value }
}

let limit = Duration { seconds: 30 }
json!{ "timeout": limit, "ready": true }      // {"timeout":30,"ready":true}
```

A member *name* is a literal, not an expression: it is matched as one so that
`"a": 1` can be told apart from a bare list of values. A name computed at run
time wants `put` or `withMember`.

## What it expands to

An ordinary chain, and nothing else:

```rune
json!{ "a": 1, "b": [2] }
```

becomes

```rune
json::object().withMember("a", (1) into json::Value)
              .withMember("b", json::array().withItem((2) into json::Value))
```

`withMember` and `withItem` hand back the value they were given, because an
`Object` and an `Array` are classes and the `Value` holds a reference to one.
That is what makes the chain work, and it means `json!` costs exactly what
writing the chain by hand would.

The parentheses around `(1)` are the macro's own: `into` binds tighter than
arithmetic, so `year + 1 into json::Value` without them would convert the `1`
and then try to add a `Value` to an integer.

## When something is wrong

Expansion is reported, so a mistake inside a `json!` says where it came from:

```
179 ║     json!{ "next": year + 1 }
                     ^^^^ ERROR: cannot apply '+' to 'i64' and 'Value'
      ─  note: in the expansion of `json!{ "next" : year + 1 }`
      ─  note:   which stands for: json::object().withMember("next", json!(year + 1))
      ─  note: in the expansion of `json!(year + 1)`
```

If none of the rules fit, the error says so and points at the invocation:

```
ERROR: no rule of macro 'json' matches these arguments
  note: none of the macro's rules fit these arguments
```

The usual cause is a member name that is not a literal, or a `:` where the
rules expect a `,`.

## Where it is visible

`json!` is a `pub macro` in `src/lib.rune`, and a `pub macro` reaches every
module of its own package and every package that builds against it — a `.rul`
carries its modules' source, so the macros come with it. Importing the package
is enough:

```rune
import json

let doc = json!{ "ready": true }
```

A macro is not reached through its module: it is written `json!`, never
`json::json!`, because expansion happens before imports mean anything.

## When not to use it

`json!` is for a shape you know when you write the program. When the shape is
decided as the program runs — a member per row, a name from a variable, a
document assembled in a loop — build it with `put` and `push` instead:

```rune
import json::value

var doc = value::Object()
for row in rows {
    doc.put(row.name, row.value into json::Value)
}
json::write(value::Value::Object(doc))
```

The two mix freely: a `json!` document can be edited afterwards, and a
hand-built value can be a member inside a `json!`.
