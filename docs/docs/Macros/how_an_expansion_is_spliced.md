# How an expansion is spliced

An expansion that is a single expression is wrapped in parentheses so it composes like the one value it is. Without that, `twice!(3) + 1` would expand to `(3) + (3) + 1` and quietly mean something else. An expansion that is several statements is spliced as it stands, since parenthesising it would not parse.

> [!NOTE]
> **Decided after expansion, not before**
>
> Which of the two is decided on the *expanded* tokens, not the rule body: a repetition hides its separator inside `$( )`, and only expansion brings it out to where it counts.
