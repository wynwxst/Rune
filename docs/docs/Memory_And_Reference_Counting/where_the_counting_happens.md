# Where the counting happens

The convention is worth knowing even though you never write it. A function returns objects **owned** — the caller inherits a count. Reading a field or a variable produces a **borrowed** value, which the compiler retains only if it needs to outlive the expression. Assigning to a field retains the new value before releasing the old one, so `x.f = x.f` is safe. Locals are released in reverse declaration order at the end of their scope, and globals in reverse declaration order after `main` returns.

> [!NOTE]
> **One value on the heap**
>
> `mem::Handle<T>` puts any single value on the heap under exactly this scheme — it is a class, so a handle is counted like anything else, and the value goes when the last handle does. Reach for it rather than an allocator.
