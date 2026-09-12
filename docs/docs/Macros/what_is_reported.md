# What is reported

| Written | Reported |
| --- | --- |
| arguments no rule fits | `no rule of macro 'x' matches these arguments`, with the definition attached |
| a macro that is not `pub`, used elsewhere | `macro 'x' is not visible here`, naming the module it is private to |
| a name that is no macro at all | `no macro named 'x'`, listing the ones that are in scope |
| a macro that expands to itself | `macro expansion did not settle after 128 rounds` |
| two macros of one name, either `pub` | `macro 'x' is declared more than once` |
| a rule without `=>` or `{ }` | the piece that is missing, at the rule |

> [!NOTE]
> **Where an error lands**
>
> Errors inside an expansion point at the invocation, which is the only place the reader wrote anything.
