# tasks

One thread doing several things at once. `async fn` marks a function whose
body runs as a task of its own; calling it starts the task and hands back a
`Future<T>`; `.await` collects the result, parking the task that asked and
letting the others run meanwhile.

```bash
rune run       # three overlapping fetches, a checksum on another thread
rune test      # the interleaving, and a future awaited twice
```

What it shows:

* `async fn` and `.await`, with an `async fn main`
* `task::all` over a `Vector` of futures
* `task::blocking` for a plain `fn` that would otherwise hold up the thread
* a class shared between two tasks with no lock — they take turns
* `task::pending` and `complete`, for a result handed over by hand
