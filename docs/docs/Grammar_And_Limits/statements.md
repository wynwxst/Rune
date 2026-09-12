# Statements

```ebnf
block         ::= "{" { statement } [ expression ] "}"
statement     ::= binding | assignment | expression | control | defer
                | declaration

binding       ::= ( "let" | "var" | "mut" ) pattern [ ":" type ]
                  "=" expression
                | identifier [ ":" type ] "=" expression
assignment    ::= place assignOp expression
assignOp      ::= "=" | "+=" | "-=" | "*=" | "/=" | "%="
                | "&=" | "|=" | "^=" | "<<=" | ">>="

control       ::= "return" [ expression ]
                | "break" [ label ] [ expression ]
                | "continue" [ label ]
defer         ::= "defer" block

ifExpr        ::= "if" expression block
                  { "elif" expression block } [ "else" block ]
whileExpr     ::= "while" expression block
loopExpr      ::= "loop" block
forExpr       ::= "for" pattern "in" expression block
member        ::= expression "." [ "$" ] ( identifier | integer )
markCall      ::= identifier "::" markPath "." identifier "(" [ args ] ")"
matchExpr     ::= "match" expression "{" { matchArm } "}"
matchArm      ::= pattern { "|" pattern } [ "if" expression ]
                  "=>" ( expression | block )
unsafeBlock   ::= "unsafe" block
```

A newline ends a statement when what precedes it can end one and what follows can begin one — inside brackets, after an operator, or before a leading `.`, it does not. Semicolons are always allowed and never required.
