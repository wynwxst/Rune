# The guards

Every rule Zombie enforces has a code and a message that says what to do about it. They are errors at every `--safety` level, because the generated code keeps no count to fall back on — the checker's verdict is what makes it sound.

| Code | The rule it enforces |
| --- | --- |
| E0270 | a `&var` borrow is exclusive — no other borrow of an overlapping place may be live at once |
| E0272 | a returned reference must outlive the call, so it may not borrow a local |
| E0273 | a value cannot be used after it is moved |
| E0274 | a value cannot be moved out of a borrow, raw memory, or a type with a `deinit` |
| E0275 | a value cannot be moved while it is borrowed |
| E0277 | a partly-moved value cannot be used whole |
| E0278 | no write to a place while it is borrowed |
| E0279 | no read of a place that is borrowed as `&var` |
| E0280 | a borrow cannot outlive the value it points at |
| E0281 | the body may borrow from no more than the result's `from` clause allows |
| E0282 | a reference result the compiler cannot trace needs an explicit `from` |
| E0283 | an argument must borrow from the place its parameter's `from` names |
| E0286 | a method may touch only the fields its view names |
| E0288 | `weak` needs a count, which single ownership does not keep |
| E0289 | a global cannot be borrowed as `&var` |
| E0290 | no write to a field through a shared `&self` |
| E0292 | a reference-counting-only declaration is unavailable |
| E0293 | a `from` place must name a parameter, `self`, or `global` |
| E0294 | `#zombie` needs a reason |
| E0296 | an internal reference must point into a heap-owned field |
| E0297 | an internal reference must borrow from a field of its own value |
| E0298 | a field that borrows another must be declared after it |
| E0299 | a written view must cover everything the body touches |

*The Zombie guards*

> [!NOTE]
> **Standard library**
>
> Findings inside the standard library are reported too, so a change that made a library body unsound is caught where it is written rather than miscompiling in silence. An ordinary compile reads only the library bodies its own code reaches through calls; `--zombie-whole-stdlib` checks every one of them, which is what the test suite does. `--no-zombie-stdlib` silences the findings if you ever need it; the bodies are read for their summaries either way.
