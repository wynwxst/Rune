# Writing `from` annotations well

A companion to [the gentle guide](zombie-guide.md). The guide introduced `from`
in passing; this document is the deep dive — what the keyword really means, the
four places you can write it, when each one earns its keep, and a set of habits
for annotating *effectively* rather than defensively.

Everything here compiles under `--memory zombie`. The error messages quoted are
the compiler's actual output.

---

## 1. What `from` actually is

In most borrow checkers a lifetime is an invented name — `'a`, `'b` — that you
thread through a signature and hope you named consistently. Rune does not do
that. In Rune:

> **A lifetime is a *place*: a parameter, a field of one, or `global`. `from`
> names that place directly.**

There is nothing to invent and nothing to keep consistent across a signature.
When you write `-> &String from a`, you are not declaring a variable `a` of kind
"lifetime"; you are pointing at the parameter `a` that is already right there and
saying "the result borrows out of *that*". A place is a real thing in your
program, so the annotation reads like a fact about your code, because it is one.

The full grammar is small:

```ebnf
OriginClause ::= 'from' ( Place | '(' Place (',' Place)* ')' )
Place        ::= ( 'self' | ident ) ( '.' ident )*   |   'global'
```

So every `from` is one of:

| Written | Means "the borrow points into…" |
|---|---|
| `from a` | the whole parameter `a` |
| `from self` | the receiver |
| `from self.text` | the `text` field of the receiver |
| `from b.items` | the `items` field of parameter `b` |
| `from (a, b)` | either `a` or `b` (you don't know which at compile time) |
| `from global` | something that lives for the whole program (a global, a string literal) |

That's the entire vocabulary. The rest of this document is about *where* you put
these clauses and *when* you should bother.

---

## 2. First rule of annotating: usually, don't

The compiler infers the origin of every returned reference from the function
body. If the body makes it obvious, you write nothing:

```rune
fn peek(&self) -> &i64 { &self.n }        // inferred: from self — no clause needed
fn first(b: &Bag) -> &Item { &b.items[0] } // inferred: from b
```

Inference is not a fallback that gives up easily — it follows the value through
the whole body, across the functions you call, and reports the union of what the
result actually depends on. For the large majority of functions this is exactly
right and an explicit clause would just be noise.

**You reach for `from` in exactly two situations:**

1. **To pin a public boundary.** Once written, the clause is the contract.
   Callers depend only on it, and a later change to the body that would borrow
   from somewhere new is caught (E0281) instead of silently widening your API.
2. **To disambiguate or to force a decision** the inference can't or shouldn't
   make for you — a result that could come from two parameters, or a boundary
   the compiler cannot see through (a raw pointer, an `extern`, a `@zombie`
   body).

Both are about *control*, not correctness. Inference is already correct; you
annotate when you want to own the boundary.

---

## 3. The four positions

`from` can appear in four places, and it means something different in each. This
is the core of the feature.

### 3.1 On a result — a promise to callers

```rune
fn longest(a: &String, b: &String) -> &String from (a, b) {
    if a.$len() > b.$len() { a } else { b }
}
```

`from (a, b)` says the returned borrow points into `a` or `b`. The caller now
knows the result stays valid exactly as long as *both* those arguments do, and
can reason about it without reading `longest`'s body.

The one rule the compiler enforces here is directional:

> **A result's `from` must name at least every place the body borrows from. It
> may name more, never fewer.**

Name fewer than the body uses and you get:

```rune
fn pick(a: &i64, b: &i64) -> &i64 from a {   // claims only `a`...
    if *a > *b { a } else { b }              // ...but the body can return `b`
}
// error: this returns a borrow of 'b', but the signature says the result borrows from a
```

The fix is to tell the truth: `from (a, b)`. This directionality is what makes
the clause a real contract — the body can never quietly borrow from somewhere
the signature didn't warn about.

### 3.2 On a parameter — a requirement *of* callers

Move the clause onto a parameter and its meaning flips: it is no longer a promise
you make, it is a demand on whoever calls you.

```rune
class List {
    head: Item
    fn first(&self) -> &Item from self { &self.head }
}

// "Whatever `item` you pass me must be borrowed from `list` — nothing else."
fn attach(list: &var List, item: &Item from list) { }
```

This is checked **at the call site** (E0283), not in `attach`'s body:

```rune
fn bad(list: &var List, other: &List) {
    let it = other.first()     // `it` borrows from `other`
    attach(list, it)           // error: 'item' must borrow from 'list'
}
```

Why would you want this? Because it lets you *tie two borrows together across a
boundary*. The signature guarantees that whatever `attach` stores into `list`
came from `list` itself, so it can't outlive the thing it's stored in. This is
the exact mechanism behind scoped threads — `thread::scope`'s `spawn` takes
`argument: A from self`, which is what forbids a thread from borrowing one of the
spawning body's own locals (they'd be gone by the time the thread ran) while
still allowing it to borrow the scope's shared environment.

Use a parameter `from` when a value flows *in and stays* — gets stored, gets
handed to another thread, gets attached to something — and you need to guarantee
where it came from.

### 3.3 On a local binding — a checked note to yourself (variable annotations)

You can put a `from` clause on a `let`, right in the middle of a function body:

```rune
fn head(list: &List) -> &Item from list {
    let first: &Item from list = list.first()   // verified against `list`
    first
}
```

This is the "variable annotation". It is **not** a contract anyone else sees —
it is a local assertion the compiler checks against the binding's initializer. It
does two jobs:

- **Documentation that can't rot.** `let first: &Item from list = …` states, at
  the point of the binding, where `first` borrows from. Unlike a comment, if a
  refactor makes `first` borrow from somewhere else, the annotation stops
  compiling. It is a comment the compiler keeps honest.
- **Narrowing an inferred union.** If an initializer's inferred origin is a union
  (`from (a, b)`) but you know — and want to rely on — the fact that in this code
  path it's really just `a`, annotating the local pins that expectation. If
  you're wrong, you find out here, at the binding, rather than three lines later
  where the borrow is used.

It's verified exactly like a result clause: the binding's actual origin must be
covered by what you wrote, or you get the same "borrows from …" mismatch. Because
of lifetime coercion (§5), a longer-lived initializer is always accepted, so
`let x: &T from a = <something global>` is fine.

**When to use it:** sparingly. On short functions the inference is right in front
of you and a local `from` adds nothing. It earns its place in longer functions
where a reference is passed around a lot and you want a fixed, checked anchor for
what it points at — or at the exact spot where a reader would otherwise have to
trace the origin by hand.

### 3.4 On a field — an internal reference

The fourth position is a struct/class/enum *field*, and here the place is always
`self.<sibling>`: the field borrows from another field of the same value.

```rune
struct Message {
    text: String                     // owns the bytes on the heap
    body: &String from self.text     // points into what `text` owns
}
```

This is the most powerful and the most constrained position. Three rules, each
with its own diagnostic:

1. **The sibling must own a heap object** — a `String`, a class, a `Vector`.
   Never inline storage, which would move with the struct and drag the pointer
   along:
   ```rune
   struct Bad { items: [4:i64], first: &i64 from self.items }
   // error: 'first' cannot borrow from 'self.items': it is stored inline and moves with the value
   ```
2. **The borrowing field is declared *after* the field it borrows from**, because
   fields are torn down in reverse declaration order — the reference must die
   before its target:
   ```rune
   struct Order { body: &String from self.text, text: String }
   // error: 'body' borrows from 'text', so it must be declared after it
   ```
3. **When you build the value, the field really must borrow that sibling** — not
   some unrelated borrow that happens to have the right type:
   ```rune
   fn wrong(text: String, other: &String) -> Message {
       Message { text: text, body: other }   // `other` isn't our `text`
   }
   // error: 'body' must borrow from the 'text' of this same value
   ```

Get all three right and you have a single owned value with valid internal
pointers — a parsed message, a cursor into a buffer — that you can move, return,
and send across threads freely, because moving it moves the *handle* while the
pointed-at bytes stay put. See §9 of the gentle guide for the intuition.

---

## 4. What counts as a place (and what doesn't)

The `from` target is resolved against real names in scope. Getting it wrong is a
clean error, not silent nonsense:

```rune
fn nope(a: &i64) -> &i64 from zzz { a }
// error: 'zzz' is not a parameter of this function
```

Rules of thumb for the target:

- In a **signature or result**, name a parameter, `self`, a dotted field path
  under one (`b.items`, `self.text`), or `global`.
- In a **field**, only `self.<sibling>` — an internal reference (§3.4).
- On a **local**, any binding in scope, plus `global`.
- Field paths follow real fields; there is no invented name and no wildcard.
- `global` is the catch-all for anything that outlives the whole call — module
  globals and string literals both qualify.

---

## 5. Coercion: you rarely have to think about `global`

A borrow of a `global` outlives *every* place a `from` clause could possibly
name. So it satisfies all of them automatically — a longer lifetime coerces to a
shorter one:

```rune
global BANNER: String = "welcome"

fn label(a: &String) -> &String from a {
    if a.$isEmpty() { &BANNER } else { a }   // returning the global is fine
}
```

The result promises `from a`, and `&BANNER` is not `a` — but it lives *longer*
than `a`, and the caller keeps `a` alive for the whole time it uses the result,
which is already more than a program-long borrow needs. The same holds for a
parameter requirement: an argument required `from list` may be a global.

The practical upshot: **don't write special cases for globals or string literals
in your annotations.** Annotate for the local, borrowed sources; the immortal
ones slot in wherever they're handed.

---

## 6. The boundary inference can't cross

There is one situation where a `from` clause is not optional but *required*: when
the result is produced somewhere the checker cannot follow. The clearest case is
a borrow conjured through a raw pointer:

```rune
fn peek(c: &Cell) -> &i64 {
    let p = &c.v as *i64
    unsafe { &*p }              // origin is invisible to the checker
}
// error: 'peek' returns a borrow but does not say from what
```

A borrow made by dereferencing a raw pointer has an *untracked* origin — it
satisfies any constraint, so the checker can't infer a specific one. The same is
true of an `extern` function, a virtual method, or a `@zombie("…")` body whose
insides aren't analysed. In all of these, **the signature is the only source of
truth**, so you must supply it:

```rune
fn peek(c: &Cell) -> &i64 from c { unsafe { &*(&c.v as *i64) } }
```

This is the design working as intended: trust is pinned at the signature, where a
reader can see it, instead of leaking out through every caller.

---

## 7. How to annotate effectively — the habits

A short checklist, in the order you should apply it:

1. **Write nothing first.** Let inference run. If it's happy and your function is
   private, you're done. Most functions never need a clause.

2. **Annotate to freeze a public boundary.** On anything other packages call,
   write the `from` you *want* callers to depend on. Now the body can change
   freely underneath it as long as it stays within the promise, and if it ever
   tries to borrow from somewhere new, E0281 tells you before your users find
   out.

3. **Name the narrowest place that covers the body.** This is the single most
   valuable habit. `from self.items` instead of `from self` is not just more
   honest — it actively unlocks callers:
   ```rune
   fn first(b: &Bag) -> &Item from b.items { &b.items[0] }

   let f = first(&bag)
   bag.count = 4          // allowed — f only borrows bag.items, not bag.count
   ```
   Had you written `from b`, the caller would be locked out of *all* of `bag`
   while `f` lived. The clause must still cover everything the body borrows
   (E0281), so the rule is: **as narrow as the body allows, never narrower.**

4. **Use a parameter `from` when a borrow flows in and stays.** Storing it,
   spawning a thread with it, attaching it to a structure — anywhere the incoming
   borrow must be tied to another argument's lifetime, put `from thatArg` on the
   parameter and let the call site prove it (E0283).

5. **Use a local `from` as a checked anchor, not decoration.** Reach for it in
   long functions to pin what a much-passed reference points at, or right where a
   reader would otherwise have to trace the origin by hand. Skip it on short
   functions where the initializer is right there.

6. **For internal references, declare target-before-referrer and build the loop
   honestly.** Heap-backed sibling (E0296), declared first (E0298), and the
   literal actually borrows it (E0297). If a field can't satisfy these, it wants
   an index into a `mem::Arena` instead of a reference.

7. **Ignore globals when choosing a clause.** Coercion means a `global` fits any
   `from`, so annotate for your live sources and let immortals slot in.

8. **When forced by a raw pointer or extern, say the smallest true thing.** The
   signature is the only truth the checker has there; make it as narrow as the
   unsafe code actually guarantees.

The through-line: **annotate to define contracts and to widen what callers can
do — not to satisfy the checker.** If you're adding a clause only to make an
error go away, first check whether the honest fix is `&` instead of a move, or
`$clone()` instead of a borrow. `from` is for shaping your interface, not for
appeasing the compiler.

---

## 8. Error quick-reference

Every `from`-related diagnostic, in plain words:

| Message (substring) | What went wrong | Fix |
|---|---|---|
| `this returns a borrow of 'b', but the signature says the result borrows from a` | Result clause names fewer places than the body borrows from. | Widen the clause: `from (a, b)`. |
| `'zzz' is not a parameter of this function` | `from` names something that isn't a place in scope. | Name a real parameter, field path, or `global`. |
| `'item' must borrow from 'list'` | A call passes an argument that doesn't come from the place the parameter's `from` requires. | Pass a borrow that comes from the named place, or relax the parameter clause. |
| `returns a borrow but does not say from what` | The result comes through a raw pointer / extern / `@zombie` body, so nothing can be inferred. | Add an explicit `from` naming what the unsafe code borrows. |
| `it is stored inline and moves with the value` | An internal ref borrows from a non-heap sibling. | Borrow from a heap-backed field (`String`, class, `Vector`), or keep an index. |
| `so it must be declared after it` | An internal-ref field is declared before its target. | Move the borrowing field below the field it borrows from. |
| `'body' must borrow from the 'text' of this same value` | A struct literal fills an internal-ref field with a borrow of something other than the named sibling. | Build the target field first and borrow the referring field from it. |

---

## 9. One-screen summary

- A `from` clause names a **place** — a parameter, a field path, or `global`. No
  invented lifetime names.
- **Result** `from` = a promise to callers; must cover everything the body
  borrows, and should name as little else as possible.
- **Parameter** `from` = a demand on callers, checked at the call; use it to tie
  an incoming borrow to another argument (scoped threads).
- **Local** `from` = a checked note to yourself; documentation the compiler keeps
  honest, or a way to pin a narrowed origin.
- **Field** `from self.sibling` = an internal reference; heap-backed sibling,
  declared after it, and actually borrowing it.
- **Coercion** means a `global`/`'static` borrow fits any clause, so you never
  annotate around immortals.
- Annotate to **shape interfaces and widen callers**, not to silence the checker.
  Start with none, and add a clause only when it buys you a contract or a choice.
