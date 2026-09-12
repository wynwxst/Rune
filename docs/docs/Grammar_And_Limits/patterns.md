# Patterns

```ebnf
pattern       ::= "_"
                | literal
                | [ "let" | "var" ] identifier
                | path [ "(" pattern { "," pattern } ")" ]
                | path "{" identifier [ ":" pattern ] { "," … } [ ".." ] "}"
                | "(" pattern { "," pattern } ")"
                | "[" pattern { "," pattern } "]"
                | expression ( ".." | "..=" ) expression
                | pattern "|" pattern
```
