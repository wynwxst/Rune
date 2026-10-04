# std::json

Reading and writing JSON. A document is a `json::Value` — one of seven shapes: the six the grammar has, plus a whole number kept apart from a fractional one so that `3` survives a round trip. Every way of looking inside answers with an `Option`, because a document is data from somewhere else; `[]` answers with a `Value` instead, and reaching into what is not there gives `null`, so a chain of subscripts ends rather than aborting halfway.

| Name | Signature | Does |
| --- | --- | --- |
| `parse` / `parseText` | `(String) -> Result<Value, Error>` | the whole text as one value; anything trailing is an error |
| `parsePrefix` | `(String, from: i64) -> Result<(Value, i64), Error>` | one value starting at `from`, and the offset it ended at — for a stream of documents in one buffer |
| `write` / `compact` / `pretty` / `prettyLine` | `(&Value) -> String` | compact, or laid out one member per line (`prettyLine` adds a final newline); the document is borrowed, so it is still there afterwards |
| `load` / `save` / `saveCompact` | `(&String) -> Result<Value, Error>` / `(&String, &Value) -> Error?` | the same, over a file; `save` writes it pretty |
| `Value` | `enum` | `Null`, `Bool`, `Int`, `Number`, `Text`, `Array`, `Object` |
| `Value::asBool` / `asInt` / `asFloat` / `asText` / `asArray` / `asObject` | `(&self) -> T?` | what is inside, if it is that; `asInt` does not round |
| `Value::get` / `index` | `(&self, String) -> Value?` / `(&self, i64) -> Value?` | a member, or an element |
| `value["name"]` / `value[0]` | `-> Value` | the same, with `Null` for what is not there |
| `Value::kind` / `length` / `isNull` |  | asking about the shape |
| `Value::withMember` / `withItem` | `(&self, …) -> Value` | building, chainably — what `json!` expands to |
| `object` / `array` / `null` | `() -> Value` | an empty one |
| `Object` | `class` | members in insertion order: `at`, `put`, `remove`, `keys`, `holds` |
| `Array` | `class` | `at`, `push`, `length` |
| `Error` | `struct { kind, line, column, offset, detail }` | what went wrong, and where; prints as a sentence |
| `errorAt` / `ofFile` | `(Kind, &String, i64, String) -> Error` / `(io::FileError) -> Error` | make one: at a byte offset, with the line and column worked out; or for a file that could not be read |
| `describeError` | `(Error) -> String` | the sentence it prints as |
| `json!` | `macro` | a document written the way JSON is written |
| `v into json::Value` |  | `i64`, `i32`, `f64`, `bool` and `String` convert |

**Parsing, reaching in, writing out**

```rune
import std::io
import std::json

fn main() -> i64 {
    match json::parse("{\"name\": \"ada\", \"tags\": [\"a\", \"b\"], \"age\": 36}") {
        Ok(doc) => {
            io::println(doc["name"])
            io::println(doc["tags"][1])
            io::println(doc["missing"]["deeper"])       // null, not an abort
            io::println(doc.get("age").unwrap().asInt() ?? 0)
            io::println(json::write(doc))
        },
        Err(e) => io::println(e),
    }
    let year = 2025
    let built = json!{ "name": "ada", "next": year + 1, "tags": ["x", null] }
    io::println(built)
    match json::parse("[1, 2,]") {
        Ok(v) => io::println(v),
        Err(e) => io::println(e),                      // says where
    }
    0
}
```

> [!NOTE]
> **Writing a document**
>
> `json!{ ... }` uses the invocation's own braces as the object's. Inside, `{ }` is an object, `[ ]` an array, `null` is null, and anything else is an expression converted with `into`. `json![1, 2]` is an array at the top level and `json!(42)` a value on its own.
