# Ownership

`Ownership.cpp` is a function-local pass that runs after a body has been type
checked. It answers four questions about the body it is given:

- Does a borrow outlive what it borrows from?
- Do two borrows of the same place overlap when one of them can write?
- Does a value that owns something a reference count cannot see get copied out of a place that still holds it?
- **Which locals never leave the scope that declared them?**

The first three are diagnostics. The fourth is not reported at all: it is an
optimisation, recorded on `VarDecl::NoEscape`. A local that never escapes and
is initialised by a construction nothing else refers to needs no reference
counting — the allocation's own count becomes the binding's, and the scope
hands it back on the way out. The code generator reads that flag and elides the
retain/release pair.

The pass is deliberately conservative in both directions. Anything it cannot
follow — a raw pointer, a borrow taken inside a closure, a place reached
through a call — is treated as escaping, so the elision is only applied where
the whole story is visible in one function body.

## It runs in parallel, and why it can

`checkOwnership(fn, diags, safety)` takes a function, a diagnostic engine and a
safety level, and nothing else. It resolves nothing, interns nothing, and
consults no table. Each call touches only that function's own tree.

That makes it the one part of checking whose units are genuinely independent,
so it does not run inline. `Sema::checkFunction` pushes the function onto
`OwnershipQueue`; `checkOwnershipOfQueued()` runs the whole queue at the end of
`check()`, over `parallelFor`.

Deferring it is safe because nothing later in checking reads what it records —
only the code generator does, and that runs afterwards.

## Determinism

Findings come back in the order the bodies were **queued**, not the order the
threads finished. Each body reports into a bucket of its own — via
`DiagnosticEngine::beginCapture` — and the buckets are replayed in sequence:

```cpp
std::vector<std::vector<Diagnostic>> found(OwnershipQueue.size());
parallelFor(OwnershipQueue.size(), [&](size_t i) {
  Diags.beginCapture(&found[i]);
  checkOwnership(OwnershipQueue[i], Diags, Safety);
  Diags.endCapture();
});
for (const auto &bucket : found)
  Diags.replay(bucket);
```

Two builds of the same program print the same diagnostics in the same order,
whatever the machine was doing at the time and whatever `RUNE_JOBS` says. That
property is worth preserving in any pass that gets parallelised later: it is
what makes build logs diffable and error-expectation tests meaningful.

## What is not checked

Nothing is checked in a generic template: its `T` is not a type yet, so whether
a value owns a resource has no answer. The *instantiation* is checked instead,
where it does. Imported declarations and library modules are skipped too — they
were checked when that library was built.
