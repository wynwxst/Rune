# Declarations

```ebnf
module        ::= { import | declaration }
import        ::= "import" path [ "as" identifier ]
path          ::= identifier { "::" identifier }

declaration   ::= { decorator } [ "pub" ] item
item          ::= function | struct | class | enum | mark | bind
                | extend | global | typealias | externBlock

function      ::= [ "async" ] "fn" identifier [ generics ] "(" params ")"
                  [ "->" type ] { where } block
params        ::= [ param { "," param } [ "," "..." ] ]
param         ::= [ label ] identifier ":" type [ "=" expression ]
                | [ "&" [ "var" ] ] "self"

struct        ::= "struct" identifier [ generics ] "{" { field } "}"
class         ::= "class" identifier [ generics ] [ ":" type ]
                  [ "+" mark { "+" mark } ] "{" { member } "}"
enum          ::= "enum" identifier [ generics ] "{" { variant } "}"
variant       ::= identifier [ "(" type { "," type } ")"
                            | "{" { field } "}" ] [ "=" expression ]
field         ::= [ "pub" ] [ "weak" ] [ "var" ] identifier ":" type
mark          ::= "mark" identifier [ generics ] [ ":" mark { "+" mark } ]
                  "{" { requirement } "}"
bind          ::= "bind" [ generics ] bindHead [ where ]
                  "{" { member } "}"
bindHead      ::= bindTarget "to" type
                | type "into" type
bindTarget    ::= markPath [ "<" type { "," type } ">" ]
                | "operator" "::" ( identifier | stringLit )
extend        ::= "extend" type [ where ] "{" { member } "}"
global        ::= "global" identifier ":" type "=" expression
typealias     ::= "type" identifier [ generics ] "=" type
externBlock   ::= "extern" stringLit "{" { externItem } "}"
externItem    ::= externFn | externVar | cxxNamespace | cxxType
cxxNamespace  ::= "namespace" identifier "{" { externItem } "}"
cxxType       ::= ( "class" | "struct" ) identifier [ generics ]
                  [ ":" identifier ] "{" { member } "}"
                | enum

generics      ::= "<" genericParam { "," genericParam } ">"
genericParam  ::= identifier [ ":" bound { "+" bound } ]
where         ::= "where" type ":" bound { "," type ":" bound }
```
