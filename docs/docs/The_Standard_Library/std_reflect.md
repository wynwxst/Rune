# std::reflect

| Function | Signature | Does |
| --- | --- | --- |
| `typeName` | `<T>() -> String` | the fully qualified name |
| `typeId` | `<T>() -> u64` | an identity to compare |
| `kindOf` | `<T>() -> Kind` | which shape `T` is |
| `sizeOf` / `alignOf` / `strideOf` | `<T>() -> usize` | layout |
| `offset_of!` | `(T, field)` | where a field begins |
| `fieldCount` | `<T>() -> usize` | how many parts `T` has |
| `fieldName` / `fieldType` | `<T>(i: usize) -> String` | the name of part *i*, or of its type |
| `conforms` | `<T, M>() -> bool` | whether `T` binds mark `M` |
| `isSend` / `isSync` | `<T>() -> bool` | whether `T` may cross or be shared between threads |
| `describe` | `<T>(value: T) -> String` | a value rendered from its layout |

*Answered while compiling; see **Compile-time reflection**.*
