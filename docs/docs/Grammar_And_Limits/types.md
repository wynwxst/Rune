# Types

```ebnf
type          ::= path [ "<" type { "," type } ">" ]
                | "&" [ "var" ] type              // borrow
                | "*" [ "var" ] type              // raw pointer
                | "[" expression ":" type "]"     // array
                | "[" type "]"                    // slice
                | "(" type { "," type } ")"       // tuple
                | type "?"                        // Option<type>
                | "@function" "(" [ type { "," type } ] ")" [ "->" type ]
                | "dyn" markPath
                | "some" markPath
                | "Self"
```
