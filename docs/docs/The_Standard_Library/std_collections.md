# std::collections

Three ways to hold a run of values, and one module each. An **array** `[5:i64]` has its length in its type. A **slice** `[i64]` carries a length beside a pointer, and an array converts to one on its own. A **`Vector<T>`** owns its storage and grows.

| Holding | Written | Length | Owns its storage |
| --- | --- | --- | --- |
| Array | `[5:i64]` | part of the type, constant | yes, inline |
| Slice | `[i64]` | carried at run time | no — it points at someone else's |
| Vector | `vector::Vector<i64>` | grows as you push | yes, on the heap |
