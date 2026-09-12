# std::any

`Any` is a builtin type, so these need no import; the module is where they are written down. See [`Any`](#any) for what they mean.

| Member | Signature | Does |
| --- | --- | --- |
| `typeName` | `(&self) -> String` | the qualified name of the type inside |
| `holds` | `<T>(&self) -> bool` | true when the value is a `T` |
| `get` | `<T>(&self) -> T?` | the value as a `T`, or `nil` |
| `expect` | `<T>(&self) -> T` | the value as a `T`; **aborts** on any other type |
