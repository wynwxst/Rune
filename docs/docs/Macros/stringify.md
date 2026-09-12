# stringify!

The argument tokens as the text they were written as. Nothing a macro body could do would produce it, because the spelling is gone by the time a body could look. It is what lets an assertion name itself:

```sh
assert!(buffer.holds(3))
  ✓ buffer.holds(3)
```

`std::testing` uses it for `assert!`, `assertEq!` and `assertNot!`, each of which also takes an explicit name when the expression is not the explanation.
