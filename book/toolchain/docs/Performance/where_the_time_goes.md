# Where the time goes

Two shapes of program, two different answers. Both are worth knowing, because
optimising for one while measuring the other is how people waste days.

## A small program

A hello world, `-O0`, on an eight-core M3, as of October 2026 — about 53 ms
end to end, 20 ms for `--check`:

| Stage | | Mostly |
| --- | --- | --- |
| `link` | ~55 % | `cc` driver and `ld`, nearly all of it waiting rather than working |
| `check` | ~20 % | signatures and the instantiations they trigger |
| process start-up | ~12 % | loading a 160 MB binary |
| `lex` + `parse` | ~5 % | |
| `machine code` | ~5 % | |

It used to be 125 ms, and the difference was the standard library. Every
compile parsed and checked all of it, and the borrow checker analysed every
one of its ~3,500 bodies. Now only the modules a program can reach are parsed
and checked, and only the bodies it reaches through calls are borrow-checked
(see [The invariants](the_invariants.md), 6 and 7).

On macOS the first run of a freshly linked program waits a few hundred
milliseconds while the system scans it. That is most of what the test suite
spends; adding the terminal under *Privacy & Security → Developer Tools*
removes it.

## A large program

`tools/rune-lsp.rune` and its modules, about 7,500 lines:

| | `-O0` | `-O2` |
| --- | --- | --- |
| before | 1,050 ms | 4,180 ms |
| after | 455 ms | 2,155 ms |

Here the front end is a small part. The back end, which turns IR into
machine code, was 80 % of an `-O0` build. It now builds an executable in
pieces on every core (codegen units, `SplitCodeGen.cpp`). What remains at
`-O2` is mostly the IR optimiser, which runs over the whole module on one
thread so that inlining sees across the pieces.

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
