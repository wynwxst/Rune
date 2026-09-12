# json

Reading and writing JSON, in four calls:

```rune
import json

let doc = json::parse("{\"name\": \"ada\", \"age\": 36}")?
doc["name"].asText()                   // "ada"
json::write(doc)                       // {"name":"ada","age":36}
json::pretty(doc)                      // the same, laid out
```

A document is a `json::Value`, which is one of seven shapes — the six the
grammar has, plus a whole number kept apart from a fractional one so that `3`
survives a round trip. Every way of looking inside one answers with an
`Option`, because a document is data from somewhere else and its shape is a
question rather than an assumption. The exception is `[]`, which answers
`null` for anything missing so that a path through a document can be written
in one line.

Start with [the guide](guide/index.md). The module reference below it is
generated from the source.
