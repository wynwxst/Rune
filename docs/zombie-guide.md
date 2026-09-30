# A gentle guide to ownership and borrowing in Zombie

This is a walk-through, in plain language, of how memory works when you compile
Rune with `--memory zombie`. No prior knowledge of borrow checkers is assumed.
Every code block here is real Rune that compiles and runs under Zombie.

If you have written Rust, a lot of this will feel familiar. If you have only
written Rune with reference counting, the good news is that most of your code
already follows these rules without you thinking about it — this guide just
makes the rules visible.

---

## 1. The one big idea

Normally, Rune keeps a little counter next to every heap value (a class, a
`String`, a closure). Each time you share the value the counter goes up; each
time a share goes away it goes down; at zero, the value is freed. That is
*reference counting* — safe, automatic, and it costs a little work at run time.

**Zombie removes the counter.** Instead, the compiler proves — before your
program ever runs — exactly *who owns each value* and exactly *when it is done
being used*. Then it frees the value at that point, with no counting at all.

You turn it on with a flag:

```bash
runec --memory zombie main.rune
```

The rule the whole system rests on is short:

> **Every value has exactly one owner. When the owner goes out of scope, the
> value is freed.**

Everything else in this guide is a consequence of that one sentence.

---

## 2. Owning a value

When you make a value and bind it to a name, that name *owns* it:

```rune
fn main() -> i64 {
    let book = "Gödel, Escher, Bach"   // `book` owns this String
    io::println(book)
    0
}                                       // `book` goes out of scope here — freed
```

You do not write anything to free `book`. The `}` at the end of the function is
where its life ends, and the compiler puts the cleanup there for you. If you
declare several values, they are freed in reverse order — last one made, first
one gone.

That is the entire lifecycle of a value: **born at `let`, freed at the closing
brace of its scope.** The rest of the guide is about what happens *in between*
when you want to use a value in more than one place.

---

## 3. Giving a value away (a "move")

Here is the first thing that surprises people. Passing a value to a function
*gives that function the value*:

```rune
class Box { var v: i64  fn init(self, v: i64) { self.v = v } }

fn take(b: Box) -> i64 { b.v }         // `take` now owns the Box

fn main() -> i64 {
    let x = Box(1)
    let n = take(x)                     // x is handed over to `take`
    take(x) + n                         // error: 'x' has been moved out of
}
```

Because a value has one owner, handing `x` to `take` means `x` is no longer
yours — you *moved* it. The second `take(x)` is asking to give away something
you already gave away, and the compiler stops you:

```
error: 'x' has been moved out of
```

This is not Zombie being difficult. It is the single-owner rule doing its job:
if two names could both "own" the Box, which one frees it? By moving, there is
never any doubt.

Moving happens whenever a value *leaves* through a by-value slot: passing it to
a function, returning it, storing it in a field, or capturing it in a `move`
closure.

**When you actually want to give a value away, this is exactly right.** The next
two sections are about the far more common case: you want to *use* a value
somewhere without giving it up.

---

## 4. Looking without taking: a shared borrow `&`

Most of the time you do not want to *own* someone else's value — you just want
to *look at it* for a moment. That is a **borrow**, written `&`:

```rune
fn borrow_book(book: &Book) {          // borrows the Book, does not own it
    println!("{} ({})", book.title, book.year)
}

fn main() -> i64 {
    let immutabook = Book {
        author: "Douglas Hofstadter",
        title: "Gödel, Escher, Bach",
        year: 1979,
    }
    borrow_book(&immutabook)            // lend it
    borrow_book(&immutabook)            // lend it again — still ours
    0
}
```

`borrow_book` gets a `&Book` — a *view* onto the book that it can read but not
keep. When the function returns, the borrow is over and `immutabook` is still
owned by `main`, ready to be lent again. You can hand out as many shared borrows
at once as you like: reading a value from ten places is never a problem.

A small convenience: when a function asks for `&Book` and you have a `Book`
sitting in a variable, you can pass it by name and Rune inserts the `&` for you.
That is why `io::println(name)` works even though `println` takes `&String` —
you did not have to write `io::println(&name)`.

---

## 5. Changing through a borrow: an exclusive borrow `&var`

A plain `&` lets you read. To *change* a value you do not own, you ask for an
**exclusive borrow**, written `&var`:

```rune
fn new_edition(book: &var Book) {
    book.year = 2014                    // allowed: this borrow can write
}

fn main() -> i64 {
    var mutabook = someBook()
    new_edition(&var mutabook)          // lend it exclusively for the change
    borrow_book(&mutabook)              // and read it again afterwards
    0
}
```

The word "exclusive" is the important part. While a `&var` borrow is alive,
**nothing else may touch the value** — no other writer, and no readers either.
This is the second half of the rule:

> **Many readers, or one writer — never both at once.**

Why so strict? Because a writer can move things around, resize a buffer, or free
something. A reader looking at the same value at the same time could be left
staring at memory that just changed underneath it. Forbidding the overlap is
what makes "no counter" safe. If you try to read a value while it is exclusively
borrowed, you get:

```
error: cannot use 'v' while it is borrowed as `&var`
```

One more consequence worth knowing: a shared `&` cannot write, *even to a class
field*. In counted mode that was allowed; under Zombie it is not:

```rune
class Counter {
    var n: i64
    fn bump(&self) { self.n += 1 }      // error: cannot assign to 'self.n' through `&`
}
```

The fix is to say what you mean — take `&var self` instead of `&self`. The
signature now honestly advertises "this method changes the counter", and callers
can plan around it.

---

## 6. Making a real second copy: `$clone()`

Sometimes you genuinely want two independent values, not a borrow. Ask for a
copy explicitly with `$clone()`:

```rune
let a = "hello"
let b = a.$clone()                      // b is a brand-new String
io::println(a)                          // a is still fine — nothing was moved
io::println(b)
```

`$clone()` allocates a fresh value and copies the contents, so `a` and `b` are
two separate owners of two separate things. It is explicit on purpose: copying a
big structure costs real work, so Rune never does it behind your back. When the
compiler tells you a value was moved and you did not mean to give it up,
`$clone()` is very often the answer.

(Small values like `i64`, `bool`, or a `char` have no heap behind them, so they
just copy freely — you never move or clone those.)

---

## 7. A borrow ends at its *last use*, not the closing brace

The compiler is precise about *when* a borrow is over. It ends at the last line
that actually uses it — not at the end of the block. This means code that looks
like it should conflict often does not:

```rune
fn main() -> i64 {
    var c = Counter(1)
    let r = c.peek()          // shared borrow of c begins
    let seen = *r             // ...last use of r is right here
    c.add(2)                  // fine — r is done, so c can be changed now
    io::println(seen.$str())
    0
}
```

`r` borrows `c`, but the borrow's whole life is just those two lines. By the
time `c.add(2)` wants to change `c`, nothing is borrowing it anymore. This is
what makes borrowing pleasant in practice: you do not have to nest everything in
tiny blocks to end borrows early — the compiler already sees where they end.

There is a matching convenience for methods. This works:

```rune
s.push(s.len())               // read s.len(), then push into s
```

Reading `s.len()` needs a shared borrow; `push` needs an exclusive one. They
look like they overlap, but the exclusive borrow for `push` only truly *takes
effect at the call*, after the argument `s.len()` has already been read. So the
common `v.push(v.len())` pattern is fine.

---

## 8. Where does a borrow come from? The `from` clause

Now the interesting part. When a function *returns* a borrow, the compiler needs
to know what that borrow points into — so it can make sure you do not keep the
borrow after its target is gone.

Most of the time it just figures this out:

```rune
fn peek(&self) -> &i64 { &self.n }      // obviously borrows from self — nothing to write
```

You only write it down when you want to *pin the contract* or when there is more
than one possibility. You write it as a `from` clause, and — this is the elegant
bit — **a "lifetime" here is just a place**: a parameter, a field, or `global`.
No invented `'a` names.

```rune
// The result borrows from a or b — say so, so the API is fixed.
fn longest(a: &String, b: &String) -> &String from (a, b) {
    if a.$len() > b.$len() { a } else { b }
}

// The result borrows from one field of the argument.
fn first(b: &Bag) -> &Item from b.items { &b.items[0] }
```

That `from b.items` is doing real work. It says the returned borrow only depends
on `b.items`, so a caller can keep the borrow *and* change a different field:

```rune
let f = first(&bag)
bag.count = 4          // fine — f only borrows bag.items, not bag.count
io::println(f.v.$str())
```

The one rule the compiler enforces here: **you may not return a borrow of a
local variable.** The local dies when the function returns, so the borrow would
point at freed memory:

```rune
fn dangling() -> &i64 {
    let n = 5
    &n                  // error: this returns a borrow of 'n', which does not outlive the call
}
```

Return the value itself, or borrow from a parameter instead.

`from` works in three places, and it means something slightly different in each:

| Where you write it | What it means |
|---|---|
| On a **result** (`-> &V from self.map`) | "this is what the returned borrow points into." A promise to callers. |
| On a **parameter** (`item: &Item from list`) | "the borrow you pass in here must come from `list`." A requirement *of* callers. |
| On a **local** (`let head: &Item from list = ...`) | "check that this really borrows from `list`." A note to yourself the compiler verifies. |

---

## 9. A struct that borrows from itself

Here is something reference counting could never express cleanly: a struct with
a field that points into *another of its own fields*.

```rune
struct Message {
    text: String
    body: &String from self.text        // body points into the text we own
}

fn parse(text: String) -> Message {
    let body = &text
    Message { text: text, body: body }  // move text in; body still points at it
}
```

Read `from self.text` as "this field borrows from the `text` of the same
value". It sounds impossible — if you move the `Message` around, wouldn't `body`
end up pointing at the old location?

No, and the reason is worth understanding. `text` is a `String`, which is a
*handle* to a heap object. Moving the `Message` moves the little handle, but the
actual text bytes stay put on the heap. `body` points at the bytes, not at the
handle, so it stays valid no matter how much the `Message` moves:

```rune
fn consume(m: Message) -> i64 { m.body.$len() }   // move the whole Message in — body is still good
```

The compiler enforces two sensible rules for these:
- The field you borrow from must be a **heap-backed** thing (a `String`, a
  class, a `Vector`), never inline storage that moves with the struct.
- The borrowing field must be **declared after** the field it borrows from, so
  it is cleaned up first.

This is what lets you build a parsed document, a cursor over a buffer, or a
tokenizer as a single owned value you can freely pass around — with the internal
pointers checked correct at compile time.

---

## 10. Touching only part of a value: views

One more precision trick. When a method takes `&var self`, it does not
necessarily need *all* of `self` — usually just a field or two. The compiler
works out which fields each method actually touches (its **view**), and uses
that to let disjoint accesses coexist:

```rune
class Factory {
    var counter: i64
    var names: [3:String]

    fn bump(&var self) { self.counter += 1 }     // view: writes { counter }

    fn count(&var self) -> i64 {
        for n in &self.names {                    // reading names...
            if n.$len() > 0 { self.bump() }       // ...while bump only touches counter
        }
        self.counter
    }
}
```

Reading `self.names` and calling `self.bump()` (which only writes
`self.counter`) do not clash, because the compiler can see they touch different
fields — even though both go through `self`. **You wrote no annotations for
this.** It is inferred.

You *can* pin a view when you want it to be part of the method's public contract,
so that a later change to the body cannot silently touch more than promised:

```rune
fn bump(&var self { counter }) { self.counter += 1 }
```

Now `bump` is documented and checked to touch only `counter`.

---

## 11. A longer life fits where a shorter one is asked

Two borrows can have different lifespans. A `global` lives for the entire
program, so a borrow of a global outlives *anything* — which means it can be
used wherever a shorter-lived borrow was expected. The compiler accepts this
automatically (a longer life "coerces" to a shorter one):

```rune
global BANNER: String = "hi from global"

fn choose(a: &String, pickGlobal: bool) -> &String from a {
    if pickGlobal { &BANNER } else { a }   // returning the global is fine —
}                                          // it outlives `a`, which is all `from a` needs
```

You rarely think about this directly; it is just the reason the "obvious" thing
works when one of your candidates happens to be a global or a string literal.

---

## 12. When the checker says no (and what to do)

Every rejection is the single-owner or one-writer rule catching something that
would be a real bug at run time. Here are the ones you will actually meet, in
plain terms, each with its fix:

| The compiler says | In plain words | What to do |
|---|---|---|
| `'x' has been moved out of` | You gave `x` away, then tried to use it. | Borrow it with `&` instead of passing by value, or `x.$clone()` before the move. |
| `cannot move 'w.b' out of a borrow` | You only borrowed `w`, so you cannot take pieces out of it to keep. | Borrow the piece (`&w.b`), or clone it. |
| `cannot move 'b' while it is borrowed` | Something still points at `b`; you cannot hand it away yet. | Finish using the borrow first (it ends at its last use). |
| `cannot use 'v' while it is borrowed as \`&var\`` | Someone has exclusive access; no one else may look right now. | Let the exclusive borrow end before you read. |
| `cannot assign to 'self.n' through \`&\`` | A shared borrow may read, never write. | Take `&var self` in the method. |
| `this returns a borrow of 'n', which does not outlive the call` | You returned a borrow of a local; it dies at the `}`. | Return the value itself, or borrow from a parameter. |
| `'inner' does not live long enough` | A borrow you kept outlives what it points at. | Declare the target in the outer scope, or keep the value, not a borrow. |

Notice the pattern: **the fix is almost always "borrow instead of move", "clone
instead of borrow", or "widen the borrow (`&` → `&var`) or narrow it back".**
Once those three moves are in your fingers, the checker mostly gets out of your
way.

---

## 13. When you truly need to decide at run time

Sometimes the compile-time rules are stricter than your program needs — you
*know* two things won't actually alias, but you can't prove it statically. Rune
gives you two runtime-checked tools for exactly these cases. Neither uses a
reference count; both simply check at run time and panic if you were wrong.

- **`mem::Checked<T>`** — a value with a runtime "is anyone using this?" flag.
  Call `.borrow()` for a read guard or `.borrowVar()` for a write guard; ask for
  a conflicting one and it panics with "already borrowed". This is the escape
  hatch when you need shared-looking access that occasionally writes.

- **`mem::Arena<T>`** — owns a pool of values and hands you small `Slot`
  handles instead of borrows. A handle to a slot that has been reused reads as
  "gone" rather than as a dangling pointer. This is the replacement for the kind
  of graph-with-back-edges you used to build with `weak` references.

Speaking of which — `weak` references are gone under Zombie, because a `weak`
reference's whole job was to notice when a count hit zero, and there is no count:

```rune
class Child { weak owner: Parent? }     // error: `weak` needs a reference count
```

The compiler points you at `mem::Arena` (keep an index) or a plain `&` borrow
instead.

---

## 14. Threads that share data

Under reference counting you might reach for an atomically-counted `Arc` to share
a value across threads. Zombie has no count, so it offers something safer and
often faster: **scoped threads**, which *borrow* shared data and are guaranteed
to finish before that data goes away.

```rune
fn worker(c: &atomic::Counter) -> i64 {
    var i = 0
    while i < 100 { c.increment(); i += 1 }
    0
}

fn main() -> i64 {
    let counter = atomic::Counter(0)
    let total = thread::scope(counter, ||(s: &thread::Scope<atomic::Counter>) -> i64 {
        let a = s.spawn(worker, s.env())    // both threads borrow the shared counter
        let b = s.spawn(worker, s.env())
        a.join()
        b.join()
        s.env().load()
    })
    io::println(total.$str())               // 200
    0
}
```

`thread::scope` lends the counter to the threads it spawns and **joins them all
before it returns**. Because the threads cannot outlive the scope, and the scope
owns the counter, every borrow a thread takes is provably safe — no counting, no
locks around the handoff, checked at compile time. The compiler even makes sure a
thread can only borrow the *scope's* environment, never one of the body's own
local variables (which would be gone by the time the thread reads them).

---

## 15. Porting your reference-counted code

The happiest fact about Zombie: **most reference-counted Rune already compiles
under it unchanged.** When something doesn't, it is almost always one of three
tiny habits:

1. **Reading a value in a function → take `&`.**
   A function that only *looks at* a `String` or a class should take `&String`,
   not `String`. Since Rune auto-inserts `&` at the call, callers don't change
   at all — and now the value isn't moved away.

2. **Wanting a second independent copy → `$clone()`.**
   Where counted code silently shared, single-ownership code moves. If you meant
   "and I still want mine", add `.$clone()`.

3. **Writing through a shared reference → `&var`.**
   A method that changes the receiver takes `&var self`, not `&self`.

That is the whole migration for the vast majority of code. The standard library
itself was ported this way, and it is written *once* — the same source compiles
under both memory modes. Anything that compiles under Zombie compiles under
reference counting too; the `from` clauses simply parse and are ignored when the
count is doing the work.

---

## 16. The cheat sheet

- **One owner per value.** Freed at the end of its scope, in reverse order.
- **Move** = give a value away (pass by value, return, store in a field). The
  old name is then unusable.
- **`&`** = borrow to read. As many at once as you like.
- **`&var`** = borrow to write. Exactly one, and nothing else may touch the
  value meanwhile.
- **`$clone()`** = make a real, independent copy.
- **Borrows end at last use**, not at the closing brace.
- **`from place`** = says where a returned (or passed, or stored) borrow points.
  A "lifetime" is just a place: a parameter, a field, or `global`.
- **A field can borrow from another field** of the same value with
  `from self.field`, if that field is heap-backed.
- **Views** let methods touch disjoint fields of `self` without conflict —
  inferred for you.
- **`mem::Checked` / `mem::Arena`** are the runtime-checked escape hatches; no
  count involved.
- **`thread::scope`** shares data across threads by borrowing, joining before
  the data dies.

The mental model to carry around is small: *who owns this, and is anyone else
looking at it right now?* Answer those two questions and you have answered the
borrow checker.
