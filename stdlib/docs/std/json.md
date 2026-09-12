# std::json

Reading and writing JSON. A document is a `json::Value`: one of seven shapes
— the six the grammar has, plus a whole number kept apart from a fractional
one so that `3` survives a round trip. Every way of looking inside answers
with an `Option`, because a document is data from somewhere else; `[]`
answers with a `Value` and gives `null` for what is not there, so a chain of
subscripts ends rather than aborting halfway.

## Parsing and reaching in

```rune
import std::io
import std::json

fn main() -> i64 {
    let source = "{\"name\": \"ada\", \"age\": 36, \"tags\": [\"a\", \"b\"]}"
    match json::parse(source) {
        Ok(doc) => {
            io::println(doc["name"])
            io::println(doc["tags"][1])
            io::println(doc["missing"]["deeper"])            // null
            io::println(doc.get("age").unwrap().asInt() ?? 0)
            io::println(doc["name"].asText() ?? "")
            io::println(doc.length())
            io::println(doc["tags"].kind())
        },
        Err(e) => io::println(e),
    }
    match json::parse("[1, 2,]") {
        Ok(v) => io::println(v),
        Err(e) => io::println(e),                            // says where
    }
    0
}
```

## Writing, and building with `json!`

`json!{ ... }` uses the invocation's own braces as the object's. Inside,
`{ }` is an object, `[ ]` an array, `null` is null, and anything else is an
expression converted with `into`.

```rune
import std::io
import std::json

fn main() -> i64 {
    let year = 2025
    let doc = json!{
        "name": "ada",
        "next": year + 1,
        "tags": ["founder", null],
        "address": { "city": "London" }
    }
    io::println(json::write(doc))
    io::println(json::pretty(doc))
    io::println(json!["x", "y"])
    io::println(json!(42))
    io::println(json::parse(json::write(doc)).unwrap()["address"]["city"])
    0
}
```

## Files

```rune
import std::io
import std::json

fn main() -> i64 {
    let path = "/tmp/rune-json-example.json"
    let doc = json!{ "port": 8080, "debug": true }
    match json::save(path, doc) {
        Some(e) => io::println(e),
        None => io::println("saved"),
    }
    match json::load(path) {
        Ok(back) => io::println(back["port"]),
        Err(e) => io::println(e),
    }
    match json::load("/no/such/file.json") {
        Ok(back) => io::println(back),
        Err(e) => io::println(e),
    }
    io::delete(path)
    0
}
```
