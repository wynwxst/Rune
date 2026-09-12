# Which of the three to reach for

| Situation | Reach for |
| --- | --- |
| The set of types is closed | an `enum`, so `match` makes the compiler check you covered it |
| The types differ, the behaviour does not | `dyn Mark`, which dispatches without asking what the type is |
| Neither: a value from outside the type system, or a container that takes whatever it is given | `Any` |

*`Any` is the last of the three, not the first*

|  | `Any` | `dyn Mark` | `enum` |
| --- | --- | --- | --- |
| Types it takes | every one | those bound to the mark | the ones listed |
| Checked by the compiler | no | the mark's methods | exhaustively, in `match` |
| Dispatch | none — you ask what it is | through a table | on the tag |
| Representation | one pointer | two words | tag plus payload |
| Cost to build | an allocation, unless it is a class | an allocation, unless it is a class | none |

*Side by side*
