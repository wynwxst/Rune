# Where the time goes

Two shapes of program, two different answers. Both are worth knowing, because
optimising for one while measuring the other is how people waste days.

## A small program

A two-file program, debug build, on an eight-core machine:

| Stage | | Mostly |
| --- | --- | --- |
| `check` | ~40 % | The standard library — the same work every time |
| `link` | ~30 % | `cc` driver and `ld` |
| `codegen` | ~10 % | |
| `lex` + `parse` | ~12 % | |
| `machine code` | ~5 % | |

The fixed cost dominates. The standard library is read in full by every
compile, and within `check` the largest pieces are signature resolution and the
generic instantiations that signature resolution triggers.

## A generic-heavy program

Six hundred distinct instantiations of a generic container:

| Stage | |
| --- | --- |
| `check` | ~93 % |
| everything else | ~7 % |

and inside `check`, body checking is nearly all of it — most of which is
monomorphisation reached from call sites.

## What this means

- **Fixed cost** is what an edit-compile loop feels. It is attacked by doing less per compile: registering LLVM's targets once rather than three times, lexing each file once rather than twice, not spawning a shell to link.
- **Scaling cost** is what a large codebase feels. It is attacked by making sure nothing in the instantiation path is linear in the number of instantiations.

## The one that was quadratic

`MethodOverloads` is keyed by `(Type *, std::string)`. Three places wanted
"every overload registered against this type" and wrote:

```cpp
for (auto &slot : MethodOverloads)          // walks the whole table
  if (slot.first.first == t)
    for (FunctionDecl *fn : slot.second)
      supplied.push_back(fn);
```

That is asked **once per generic instantiation**, and the table **grows with
every instantiation** — so the total cost is the square of how much generic
code the program uses. On the six-hundred-instantiation benchmark it was about
35 % of the entire compile.

The map is ordered, so one type's slots are contiguous:

```cpp
void appendOverloadsFor(Type *target, std::vector<FunctionDecl *> &out) const {
  for (auto it = MethodOverloads.lower_bound({target, std::string()});
       it != MethodOverloads.end() && it->first.first == target; ++it)
    out.insert(out.end(), it->second.begin(), it->second.end());
}
```

`std::pair` compares lexicographically, so `{t, ""}` is at or before every
`{t, name}` and strictly before every `{t', …}` with `t' > t`. Same answer,
`O(log n + k)` instead of `O(n)`.

**The lesson generalises.** Any loop over a whole table inside the
instantiation path is a quadratic waiting to happen. If you need a subset of an
ordered map keyed by a pair, seek to it.
