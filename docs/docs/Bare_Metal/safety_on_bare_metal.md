# Safety on bare metal

Nothing about checking changes. `--safety full` inserts every check it inserts anywhere — array bounds, integer overflow, division by zero, a nil dereference, a `match` no arm matched, a drop of an object that turns out to be shared — and the Zombie borrow checker proves the same things at compile time. The only difference is where a failed check goes: the freestanding runtime formats it into a buffer of its own, not the heap, and hands it to the `@panicHandler`.

```sh
KERNEL PANIC: index 4 is out of bounds for a collection of length 4
  at main.rune:53:34
```
