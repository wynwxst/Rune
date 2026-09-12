# Adding a diagnostic

## The mechanics

```cpp
Diags.error(range, "'{}' is not bound to mark '{}'", ty->toString(), markName(mk))
     .note("add `bind {} to {}` to satisfy this bound", markName(mk),
           ty->toString())
     .related(declaredAt, "the bound on this parameter",
              "the call above did not meet it")
     .code(241);
```

| Call | Gives you |
| --- | --- |
| `error` / `warn` / `note` / `remark` | Severity |
| `fatal` | A locationless, driver-level failure |
| `.note(...)` | A `─ note:` line |
| `.related(range, label, hint)` | A second snippet with a `└▶` connector |
| `.code(n)` | `[E0241]` |
| `.at(range)` | Overrides the primary span |

`{}` is Rune's own formatter in `Diagnostics.h`, not `printf`.

## Picking a code

Codes are `E%04u` or `W%04u` and grouped loosely by phase — roughly 100s for
the driver and file directives, 200s for the checker, 500s upward for later
passes. There is no registry, so grep before you choose:

```sh
grep -rn 'code(241)' runec/src
```

Reusing a number that already means something else is the one way to get this
wrong.

## House style

Rune's diagnostics are unusually chatty on purpose. Three rules hold across the
compiler:

**Say what to do, not just what is wrong.** Every `.note` in the codebase is
either an instruction or the missing piece of context. "cannot find 'x' in this
scope" is followed by "check the spelling, or add an `import` for the module
that declares it".

**Point at both ends.** If the mistake is a mismatch, `.related` the other end.
A bound that was not met shows the call *and* the declaration that required it.

**Explain the rule when the rule is surprising.** `Send` and `Sync` are not
bound by the user — the compiler reads them off the type — so telling someone
to `bind Send` would be telling them to defeat the check. The diagnostic says
so instead.

## Warnings

`Diags.warn(...)` respects `-w` (suppress) and `-Werror` (promote). `.code()`
formats as `W` automatically when the severity is a warning — the flag is taken
from the diagnostic, not from the number.

## Do not report twice

If the value you are complaining about is already `Type::isError()`, say
nothing: something has already reported, and a second diagnostic about a
poisoned type is noise. Return `Types.errorType()` from a failing rule so
everything above you does the same.

## Test it

`tests/cases/` supports expectations for failure as well as success:

```rune
// EXPECT-ERROR: is not bound to mark
```

The check is a substring match against the compiler's output, so quote enough
to be unambiguous and little enough that rewording the message later does not
break the test.
