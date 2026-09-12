# The Rune language reference

This document describes the language as implemented. Anything marked
**Not yet implemented** parses and type-checks but has no code generation, or
is not accepted at all; those notes are deliberate rather than aspirational.

What the language does *not* do, and what might be done about it, is
[`roadmap.md`](roadmap.md) — a design document rather than a promise.

---

## 1. Lexical structure

### Statement termination

Semicolons are optional. A line break ends a statement when the preceding token
can end one — an identifier, a literal, a closing bracket, or a keyword such as
`return`. Otherwise the statement continues:

```rune
total = a +
        b            // one statement: `+` cannot end a statement

value = compute()
    .then()          // one statement: a line starting with `.` continues it
```

Inside `(` … `)` and `[` … `]` line breaks are never terminators. An explicit
`;` always ends a statement, and it is what distinguishes a discarded value
from a block's result.

### Comments

```rune
// line comment
/* block comment, which /* nests */ correctly */
```

### Literals

```rune
42        1_000_000    0xFF      0b1010     0o755
42i32     255u8        1.5       1.5f32     1e-3
true      false        nil
'a'       '\n'         '\u{1F600}'
"text"    "escape \t sequences \u{2713}"
r"raw \ string"        r#"raw with "quotes""#
```

Escapes: `\n \t \r \0 \\ \" \' \e \xNN \u{...}`.

An unsuffixed integer literal adopts the type its context wants, defaulting to
`i64`; an unsuffixed float literal defaults to `f64`.

---

## 2. Types

| Category | Spelling |
|---|---|
| Signed integers | `i8` `i16` `i32` `i64` `isize`, alias `int` |
| Unsigned integers | `u8` `u16` `u32` `u64` `usize`, aliases `uint`, `byte`, `Byte` |
| Floating point | `f32` (alias `float`), `f64` (alias `double`) |
| Boolean | `bool` |
| Text | `Character` (one Unicode scalar), `CString` (borrowed NUL-terminated bytes, for FFI), `String` (owned, reference counted, UTF-8) |
| Array | `[5:i64]` — fixed length |
| Slice | `[i64]` — pointer plus length |
| Tuple | `(i64, String, bool)`, `()` for unit |
| Optional | `T?`, which *is* `Option<T>` |
| Result | `Result<T, E>`; a bare `T` or `E` promotes into it |
| Borrow | `&T`, `&var T` |
| Unique owner | `Unique<T>` — one owner, moved rather than copied (§16) |
| Raw pointer | `*T`, `*var T` — unsafe |
| Function | `@function(i64, i64) -> bool`, or `@function(bool, i64, i64)` |
| Nominal | `struct`, `class`, `enum`, `mark` |
| Dynamic | `Any` — one value of any type, asked at run time what it is (§5a) |

### Function types

Written the way a `fn` is: the parameters in parentheses, then `->` and the
result. Leave the arrow off and the function returns nothing.

```rune
type Predicate = @function(i64) -> bool     // takes an i64, returns a bool
type Sink = @function(i64)                  // takes an i64, returns ()
type Producer = @function() -> i64          // takes nothing, returns an i64
```

### Conversions

Widening happens implicitly: `i32` to `i64`, `f32` to `f64`, `&var T` to `&T`,
`[N:T]` to `[T]`, a subclass to its base, `T` to `T?` (that is, to
`Option::Some(value)`), any conforming type to `dyn Mark`, and anything at all
to `Any`. Everything else needs `as`:

```rune
small = big as i32
bits  = value as u64
text  = cstr as String
```

Pointer casts and integer/pointer conversions are unsafe operations.

---

## 3. Bindings and scope

```rune
x = 7                    // immutable, type inferred
x: i64 = 7               // immutable, type written
let x = 7                // the same, said explicitly
let x: i64 = 7
var count = 0            // mutable
mut count = 0            // `mut` is a synonym for `var`
var buffer: [64:u8]      // mutable, uninitialised
let (a, b) = (1, 2)      // destructuring
global var counter: i64 = 0
global PI: f64 = 3.14159
```

Bindings are immutable unless declared with `var`. `let` is optional — a bare
`name = value` declares one just the same — but it reads clearly next to a
`var` and makes the intent obvious in a long block. Assigning to an immutable
binding is an error that points at both the assignment and the declaration.

A name may be bound in a nested block without disturbing the outer one.
Bindings are function- or block-scoped; `global` declares module scope.

### Blocks are expressions

```rune
x = {
    t = 1 + 1
    t * 2                // no semicolon: this is the block's value
}
```

The same rule gives functions their result:

```rune
fn double(n: i64) -> i64 {
    n * 2                // implicit result
}
```

A function with no `->` returns `()`, and a trailing expression in such a
function is evaluated and discarded.

---

## 4. Functions

```rune
fn add(a: i64, b: i64) -> i64 { a + b }

fn greet(name: String, greeting: String = "hello") -> String {
    greeting + ", " + name
}

// Arguments may be positional, labelled, or a mix.
greet("world")
greet("world", greeting: "hi")
greet(name: "world", greeting: "hi")
```

Nested functions are allowed and behave like private module-level functions.

### Closures

```rune
add   = ||(a: i64, b: i64) -> i64 { a + b }
scale = 3
times = ||(n: i64) -> i64 { n * scale }   // captures `scale` by value
```

Captures are copied when the closure is created, and reference-counted captures
are retained for the closure's lifetime. A named function converts to a
function value automatically.

---

## 5. Data types

### Structs

```rune
struct Point {
    pub x: f64
    pub y: f64
    label: String = "origin"      // field default
}

p = Point { x: 1.0, y: 2.0 }
q = Point { x: 3.0, ..p }         // take the rest from p
```

Fields are private unless marked `pub`. Field defaults are evaluated at each
construction site.

A struct may declare a `deinit`, which runs when the value it lives in is
destroyed. That is what lets a value own something reference counting cannot
see — a descriptor, a lock — and it makes the value moved rather than copied
when it is handed on. See §16.

### Arrays and slices

An array has a fixed length that must be a constant expression: a literal,
arithmetic on literals, or an immutable global.

```rune
let LANES: i64 = 4
let grid: [LANES * 2:i64] = [0; LANES * 2]
var block: [1 << 6:u8]
```

`values[a..b]` borrows part of an array or slice as a `[T]`, which is a pointer
and a length. The bounds are checked like any other index.

```rune
let all: [6:i64] = [1, 2, 3, 4, 5, 6]
let head = all[0..3]          // [i64] of length 3
let tail = all[3..]           // to the end
let inclusive = all[0..=2]    // same as 0..3
```

### Enums

```rune
enum Shape {
    Empty,
    Circle(f64),
    Rect { width: f64, height: f64 },
}

enum Status { Ok = 0, NotFound = 404 }
```

Enums with no payload convert to integers with `as`.

### Classes

Classes are reference types with single inheritance and virtual methods.

```rune
class Animal {
    pub name: String

    fn init(self, name: String) { self.name = name }
    fn deinit(self) { /* runs when the last reference goes */ }

    pub fn speak(&self) -> String { "..." }
}

class Dog : Animal {
    fn init(self, name: String) { super.init(name) }
    pub fn speak(&self) -> String { "woof" }     // overrides
}

d = Dog("Rex")                    // calling the class runs `init`
d.speak()                         // "woof", dispatched dynamically
```

`init` and `deinit` are both optional. `super.method()` calls the base
implementation without going through the vtable. `value is SomeClass` tests the
dynamic type.

### `self`

| Written | Meaning |
|---|---|
| `self` | takes the receiver by value (or the reference, for a class) |
| `&self` | borrows the receiver immutably |
| `&var self` | borrows the receiver mutably |

---

## 5a. `Any`

`Any` holds one value of any type and remembers which. It is a builtin type,
so its methods need no import.

```rune
let boxed: Any = 42          // anything converts, implicitly

boxed.typeName()             // "i64"
boxed is i64                 // true
boxed.holds::<i64>()         // true — the same question, as a method
boxed.get::<i64>()           // Some(42)
boxed.get::<String>()        // nil
boxed.expect::<i64>()        // 42, and aborts if it were not one
```

| Written | Means |
|---|---|
| `value.typeName()` | the fully qualified name of the type inside |
| `value is T` | true when the value inside is a `T` |
| `value.holds::<T>()` | the same test, where `T` has no bare-name spelling |
| `value.get::<T>()` | `T?` — the value, or `nil` when it is something else |
| `value.expect::<T>()` | `T` — the value, aborting when it is something else |

`get` is the one to reach for: the test and the read happen together, so there
is no way to read the value as a type it does not have. `expect` is for a type
that is an invariant of the program rather than something to be checked; it
names both types on the way out.

`is T` and `holds::<T>()` ask the same question. `is` reads better but only
takes a name, so a type with no bare-name spelling — `[3:f64]`, `(i64, String)`,
`dyn Shape` — goes through `holds` and `get`.

### What the answer is based on

An `Any` is one pointer: the value itself when it is a class, and a
reference-counted box around it otherwise. Either way the object header in
front of it carries a descriptor naming the type, and every question above is
answered from that descriptor — from the value, never from a promise the
program made about it. Each type gets one descriptor across the whole program,
so the comparison is a pointer comparison.

A class is matched the way `is` matches classes everywhere: a `Dog` also holds
as an `Animal`. Everything else is matched exactly — a `u32` does not hold as
an `i64`, even though one converts to the other.

An `Any` owns what it holds: boxing retains, and releasing the `Any` releases
the value. `get` and `expect` hand back a copy, retained. A `Unique<T>` cannot
go in at all — an `Any` would be its second owner, and it has room for one.

### When to reach for it

| Situation | Reach for |
|---|---|
| The set of types is closed | an `enum`, so `match` makes the compiler check you covered it |
| The types differ, the behaviour does not | `dyn Mark`, which dispatches without asking what the type is |
| Neither — a value from outside the type system, a container that takes whatever it is given | `Any` |

`Any` costs an allocation on the way in (except for a class, which is already
an object) and a check on the way out. Nothing dispatches through it: the only
thing you can do with an `Any` is ask what it is.

Nothing prints an `Any` by default, because nothing can know how. A program
that wants one to print says so:

```rune
bind io::Display to Any {
    fn display(&self) -> String {
        if self is i64 { return self.expect::<i64>().$str() }
        if self is String { return self.expect::<String>() }
        "<" + self.typeName() + ">"
    }
}
```

---

## 6. Marks

Marks are Rune's traits. They may require methods, supply defaults, and require
other marks.

```rune
mark Show {
    fn show(&self) -> String                       // requirement
    fn shout(&self) -> String { self.show() + "!" } // default
}

mark Measured: Show {                              // super-mark
    fn magnitude(&self) -> f64
}
```

`bind` implements a mark for a type:

```rune
bind Show to Point {
    fn show(&self) -> String { "(" + self.x.$str() + ", " + self.y.$str() + ")" }
}

bind<T> Show to Wrapper<T> where T: Show {
    fn show(&self) -> String { "Wrapper(" + self.value.show() + ")" }
}
```

A conditional bind applies only when its `where` clause holds, so
`Wrapper<i64>` above gains `show` only if `i64` is itself bound to `Show`.

Each bind gets its *own copy* of the mark's default methods, so inside a
default, `Self` is the concrete type and sibling calls resolve to that type's
implementations.

### `Self`, and requirements that construct

A requirement with no `self` parameter is static, and `Self` in its signature
stands for the implementing type. That is how a mark describes a constructor:

```rune
mark Animal {
    fn new(name: String) -> Self       // static: no `self`
    fn noise(&self) -> String
}

bind Animal to Sheep {
    fn new(name: String) -> Self { Sheep { naked: false, name: name } }
    fn noise(&self) -> String { "baaaa" }
}

let dolly: Sheep = Animal::new("dolly")   // the annotation picks the bind
```

A static requirement has no receiver, so the implementation is chosen by the
type the result flows into — an annotation, a parameter, or a return type. With
nothing to infer from, the compiler asks for one. Static requirements are not
reachable through `dyn Mark`: a mark object carries one implementation chosen
at run time, and a call with no receiver has nothing to choose from.

`Self` also works as a parameter type, so a mark can describe an operation over
its own type (`fn combine(&self, other: Self) -> Self`), and an implementation
must match the requirement once `Self` is read as the implementing type.

### Which method a call finds

A type's own methods — its body and its `extend` blocks — always answer
`value.name()`. A `bind` fills in what the type does not already have; it never
displaces it, and warns if you write one that would. The mark's version stays
reachable by name:

| Written | Finds |
| --- | --- |
| `value.name()` | the type's own method, or a mark's if it has none |
| `value::Mark.name()` | the method `Mark` binds for this type |
| `self.name()` inside a bound method | that mark's own `name` |
| `Mark::name(...)` | the static requirement, for the type in context |

Where a `bind` supplied the name more than once — see [Overloading by
right-hand type](#overloading-by-right-hand-type) — the arguments decide which
of the versions each of these reaches.

`value::Mark.name()` takes a plain name on the left; bind an expression to one
first. A default body always calls the mark's own requirements rather than the
type's lookalikes — it was written against the mark, so that is what it gets.

A field and a method may share a name. The syntax settles it: `self.name` is
the field, `self.name()` the method, unless the field itself holds something
callable.

`extend` adds inherent methods to an existing type:

```rune
extend Point {
    fn scaled(&self, k: f64) -> Point { Point { x: self.x * k, y: self.y * k } }
}
```

Marks and extensions apply to **builtin types too**, which is what makes a
mark like `Display` work uniformly:

```rune
bind io::Display to i64 { fn display(&self) -> String { self.$str() } }

extend i64 {
    fn squared(&self) -> i64 { self * self }
}
```

On a builtin, `&self` passes the value rather than a borrow — there is nothing
to look inside — so `self * self` above means what it reads as.

### Mark objects

A generic parameter with a mark bound is resolved at compile time. When the
concrete type is only known at run time — a collection of several unrelated
types — use `dyn Mark`, a value paired with a dispatch table:

```rune
fn describe(s: dyn Shape) -> String { s.summary() }

let mixed: [3:dyn Shape] = [Circle { radius: 1.0 }, Rect { .. }, Tile::Large]
for s in mixed { total += s.area() }
```

Anything bound to the mark can be stored in one, including builtins, enums and
classes. A value type is copied into a reference-counted box; a class is
already an object and is used directly. Prefer the generic form when the type
is known at the call site: it is dispatched statically and can be inlined.

### `into`: conversions you write yourself

`as` has fixed meanings — numbers, enums, pointers — and no binding can change
them. A conversion between two of your own types is `into`, which dispatches
through the built-in `As` mark:

```rune
struct Celsius { v: f64 }
struct Fahrenheit { v: f64 }

bind Celsius into Fahrenheit {
    fn convert(&self) -> Fahrenheit { Fahrenheit { v: self.v * 1.8 + 32.0 } }
}

let f = c into Fahrenheit
```

That binding is the same as `bind As<Fahrenheit> to Celsius`. The mark takes
the destination as its argument, so one type may convert into as many others
as it likes, and `As<Target>` works as a bound:

```rune
fn isWarm<T: As<Fahrenheit>>(v: T) -> bool { (v into Fahrenheit).v > 80.0 }
```

`As` is declared in `std::convert` and put in scope by the prelude, so a
binding never needs an import.

### Decorators you write yourself

Any name that is not one of the built-ins is a decorator the program declares:
a function whose **last parameter is a function**. The earlier parameters are
the decorator's own arguments; the decorated function fills the last one.

```rune
fn route(method: String, path: String, handler: @function() -> ()) { ... }

@route("GET", "/health")
fn health() { ... }
```

`@route("GET", "/health")` on `health` means `route("GET", "/health", health)`,
called **once before `main`**, after the globals exist and in the order the
decorators were written — which is what makes a registry work.

The rules: the last parameter has to be a function, the two shapes have to
agree, the decorator cannot be generic (nothing could infer its arguments that
early), it applies to free functions only, and a built-in name always wins. An
unknown decorator is an error rather than something the compiler ignores.

### Overloading by right-hand type

An operator is overloaded **once per right-hand type**, so a type may define
`+` against several others. Which one runs is decided by what is on the right,
including for compound assignment — the two sides need not be the same type.

```rune
bind operator::add to Money {
    fn add(&self, rhs: &Money) -> Money { ... }
}
bind operator::add to Money {
    fn add(&self, rhs: &Rate) -> Money { ... }
}

var running = Money { cents: 100 }
running += Rate { percent: 50 }        // picks the Rate overload
```

An exact match on the right-hand type wins; failing that, the first overload
whose parameter would accept the value.

This is not special to arithmetic, and not special to operators. **Any method
a `bind` supplies may be written more than once, as long as the versions take
different things.** The same mark may be bound to one type several times over,
and together the versions form an overload set:

```rune
mark Add { fn add(&var self, item: String) }

bind Add to Cart { fn add(&var self, item: String) { ... } }
bind Add to Cart { fn add(&var self, cents: i64) { ... } }
bind Add to Cart { fn add(&var self, item: String, cents: i64) { ... } }

cart.add("pear")            // the first
cart.add(200)               // the second
cart.add("pear", 200)       // the third
```

`index` and `indexSet` overload the same way, which is how one type can be
subscripted both by position and by name — the write follows whichever `index`
the read chose:

```rune
bind operator::"[]" to Value {
    fn index(&self, key: String) -> Value { ... }
}
bind operator::"[]" to Value {
    fn index(&self, at: int) -> Value { ... }
}
```

A `fn` may still not be declared twice. A `bind` is the only place in the
language where one name has several bodies.

Selection is by argument type, exact before widening. A call that two versions
accept equally well is reported rather than decided by declaration order, and
so is a call no version accepts — with every candidate listed. Everything else
about the call works as it always does: a labelled argument goes to the
parameter of that name, positional ones fill what is left in order, and a
version with a defaulted tail is a candidate for the shorter call too. Two bindings
that take the *same* things are still two answers to one question, and are
reported as equally specific. Specificity comes first: a written-out target
beats a parameterised one whatever either of them takes, so a narrower binding
replaces a wider one rather than overloading against it.

Only one version answers a mark's requirement — the one whose signature
matches it. That is what a `dyn Mark` value calls; the rest are reachable on
the concrete type, and through `value::Mark.name(...)` when the arguments say
which.

A builtin's own meanings stay the language's, but the pairs it has *no* meaning
for are yours to give. `String + String` is built in and cannot be replaced;
`String + Character` is not, so `std::io` defines it and `text += 'a'` works.
The rule is one sentence: **an overload may teach a type to work with another
type, but never replace what the language already does.**

### `@alias`: a second name

`@alias("other")` puts a declaration in scope under another name as well as its
own; more than one is allowed. The name is a string, so it can hold characters
an identifier cannot:

```rune
extend Bag {
    @alias("size")
    pub fn length(&self) -> i64 { self.count }
}

@alias("makeBag")
fn bag(n: i64) -> Bag { Bag { count: n } }
```

An alias is a name, not a copy — one body, one symbol. On a `bind operator::…`
block it registers a further spelling for that operator everywhere, which is
the one case where an alias is not local to what it decorates.

### Operator overloading

An operator is named by a word, a bracket name, or the punctuation itself in
quotes: `operator::index`, `operator::LeftSquareBracket` and `operator::"[]"`
are three spellings of one thing. The method inside claims the operator, so a
block may carry more than one.

`deref` and `derefSet` make a value behave like a pointer — `*v` reads, `*v = x`
writes, and `*v += x` does both, evaluating the receiver once:

```rune
bind operator::"*" to Celsius {
    fn deref(&self) -> f64 { self.deg }
    fn derefSet(&var self, value: f64) { (*self).deg = value }
}

var c = Celsius { deg: 21.5 }
*c += 2.5              // 24.0
```

`std::mem` binds both for `Handle<T>`, so a handle reads like a pointer to the
value it owns:

```rune
var h = mem::Handle<i64>(41)
*h = *h + 1            // 42
```

```rune
bind operator::add to Vec2 {
    fn add(&self, rhs: &Vec2) -> Vec2 { ... }
}

bind operator::LeftSquareBracket to Json {
    fn index(&self, key: String) -> Json { ... }
}
```

Both word and bracket spellings are accepted and normalised:

| Operator | Method | Also spelled |
|---|---|---|
| `+` `-` `*` `/` `%` | `add` `sub` `mul` `div` `rem` | `Plus`, `Minus`, `Star`, `Slash`, `Percent` |
| `&` `\|` `^` `<<` `>>` | `bitand` `bitor` `bitxor` `shl` `shr` | `Ampersand`, `Pipe`, `Caret`, `ShiftLeft`, `ShiftRight` |
| `==` `!=` | `eq` | `EqualEqual` |
| `<` `<=` `>` `>=` | `cmp` | `LessThan`, `Compare` |
| `-x` `!x` `~x` | `neg` `not` `bitnot` | `Negate`, `Bang`, `Tilde` |
| `a[i]` | `index` | `LeftSquareBracket`, `Subscript` |

`eq` returns `bool`; `cmp` returns an ordering integer (negative, zero,
positive) and drives all four relational operators. Compound assignment
(`+=`) uses the corresponding binary overload.

---

### `where` on a requirement

A requirement may carry type parameters and a `where` clause of its own, and
so may a mark's default method. The clause is enforced wherever the method is
called — through the type, and through the mark:

```rune
mark Sink {
    fn accept<T>(&var self, value: T) -> String where T: Show

    fn acceptTwice<T>(&var self, value: T) -> String where T: Show {
        self.accept(value) + self.accept(value)
    }
}

mark Holder {
    type Item
    fn render(&self) -> String where Self::Item: Show   // about the associated type
}

fn record<S: Sink>(s: &var S) { s.accept(7) }    // `i64: Show` is checked here
```

An implementation may ask for *more* than the mark promised — a bind whose
`accept` says `where T: Show, T: Total` compiles — but the extra bound is then
enforced at every call, including the ones that only knew about the mark.

## 7. Generics

```rune
fn largest<T>(a: T, b: T, better: @function(T, T) -> bool) -> T {
    if better(a, b) { a } else { b }
}

fn render<T: Show>(value: T) -> String { value.shout() }

struct Wrapper<T> { value: T }
```

Type arguments are inferred from the call, or written explicitly with a
turbofish: `render::<Point>(p)`. Generics are monomorphised: each distinct set
of arguments produces its own specialised function or type. Template bodies are
checked once per instantiation, and a diagnostic inside one points back at the
call site that produced it.

A method may have generic parameters of its own, written the same way:

```rune
class Box {
    pub fn pick<T>(&self, v: T) -> T { v }
    pub fn width<T>(&self) -> i64 { mem::size_of<T>() as i64 }
}

box.pick(3)             // T inferred from the argument
box.width::<f64>()      // nothing to infer from, so the call site says
```

A generic method is dispatched from the type at the call site, never through a
vtable — each set of arguments is a different function, so there is no single
address a slot could hold.

---

### Specialisation

More than one `bind` may apply to one type. The narrower one wins, and which
is narrower does not depend on the order they were written in:

```rune
bind<T> Show to Wrapper<T> { fn show(&self) -> String { "some wrapper" } }
bind Show to Wrapper<i64>  { fn show(&self) -> String { "a wrapped i64" } }

(Wrapper<i64> { value: 1 }).show()      // "a wrapped i64"
(Wrapper<f64> { value: 1.0 }).show()    // "some wrapper"
```

Three things make one binding narrower than another, in that order:

| Narrower | Than |
|---|---|
| a target written out — `Wrapper<i64>` | one with parameters — `Wrapper<T>` |
| more of the shape pinned down — `Boxed<Boxed<T>>` | less — `Boxed<T>` |
| more asked of the parameters — `where T: Tag` | less — no clause at all |

Two bindings of one mark that are equally specific are **reported**, not
decided by the order they were reached in:

```rune
bind<T> Show to Wrapper<T> where T: Tag  { ... }
bind<T> Show to Wrapper<T> where T: Mood { ... }
// error, for a `Wrapper<i64>` whose `i64` is both:
//   two bindings of 'show' are equally specific for 'Wrapper<i64>'
```

Clauses that cannot both hold are not a clash — `Wrapper<i64>` takes the first
and `Wrapper<String>` the second — and neither are two *different* marks that
happen to name a method the same way. `Iterator` and `Sequence` both supply
`map`, and which one answers is settled by the mark asked for.

### Binding a shape

A `bind` target may be a shape rather than a declaration — `[T]`, `[N:T]`,
`(A, B)`, `&T` — and then every type of that shape gets the binding:

```rune
bind<T> io::Display to [T] where T: io::Display {
    fn display(&self) -> String { ... }
}
```

An array is usable wherever a slice is, so a binding written for `[T]` covers
both. `std::io` uses exactly this, which is why an array prints without
anything having to be written for its element type:

```rune
io::println([1, 2, 3])          // [1, 2, 3]
io::println((7, "seven"))       // (7, seven)
```

The `where` clause is what decides which shapes are covered: a slice of
something that is not `Display` is not `Display` either.

## 8. Control flow

Every construct below is an expression.

```rune
label = if n < 0 { "negative" } elif n == 0 { "zero" } else { "positive" }

while total < limit { total += step }

total = loop {
    n -= 1
    if n == 0 { break total }     // `loop` can carry a value out
}

for i in 0..10 { }                // exclusive
for i in 0..=10 { }               // inclusive
for item in items { }             // arrays and slices
for item in vec { }               // anything bound to Iterator or Sequence
for (x, y) in cells { }           // the binding is a pattern (§8a)

outer: loop {
    for i in 0..10 {
        if i == 5 { break :outer }
        if i == 2 { continue :outer }
    }
}
```

Loop labels are written `name:` before the loop and referenced as `:name`.

### match

```rune
description = match value {
    0 => "zero",
    1 | 2 | 3 => "small",
    4..=9 => "medium",
    n if n > 1000 => "huge",
    _ => "large",
}

area = match shape {
    Shape::Empty => 0.0,
    Shape::Circle(r) => PI * r * r,
    Shape::Rect { width, height } => width * height,
}
```

Patterns: literals, ranges, `_`, bindings, tuples, `&` references, enum
variants (tuple and struct shaped), struct destructuring with `..`, slices
and arrays, and `|` alternatives. Guards are written `pattern if condition`.

A slice pattern takes a run of elements apart. `..` stands for any number in
the middle, and `..rest` binds them as a slice into the same storage:

```rune
fn sum(xs: [i64]) -> i64 {
    match xs {
        [] => 0,
        [head, ..tail] => head + sum(tail),
    }
}

match words {
    ["go", dir] => "going " + dir,
    [first, .., last] => first + "..." + last,
    _ => "?",
}

let [a, b, c] = triple           // irrefutable: a fixed array of three
for [x, y] in pairs { ... }
```

A slice is matched by length, and a `match` over one is exhaustive when
every length has an arm. A fixed array's length is known, so a pattern that
accounts for every element is irrefutable — it works in `let` and `for` — and
one that cannot line up is an error.

Matches over enums must be exhaustive; the compiler lists the variants you
missed. Alternatives joined with `|` may not bind names.

`if value is Pattern { ... }` tests and binds in one step.

Both `match` and `is` look through borrows, so a `&self` method can match on
`self` directly without dereferencing it first.

---

## 8a. Iterators

`for` walks three shapes natively — a range, an array, a slice. Everything
else it asks, through two marks that are in scope everywhere:

```rune
mark Iterator {
    type Item
    fn next(&var self) -> Self::Item?
}

mark Sequence {
    type Iter: Iterator
    fn iterate(&self) -> Self::Iter
}
```

An `Iterator` is a cursor: `next` hands back a value each turn and `nil` when
it runs out. A `Sequence` is something a fresh cursor can be had from.

```rune
struct Countdown { pub remaining: i64 }

bind Iterator to Countdown {
    type Item = i64
    fn next(&var self) -> Self::Item? {
        if self.remaining <= 0 { return nil }
        self.remaining -= 1
        self.remaining + 1
    }
}

var down = Countdown { remaining: 3 }
for n in down { io::println(n) }          // 3 2 1
```

A container binds `Sequence` rather than `Iterator`, because a container is
not a cursor over itself. It hands out a fresh one, so two loops over the same
container — nested loops included — do not interfere.

```rune
bind Sequence to Grid {
    type Iter = GridCells
    fn iterate(&self) -> GridCells { GridCells { grid: *self, at: 0 } }
}
```

`Iterator` is asked first, so a type that is both stays its own iterator.

A `for` sequence cannot be a bare struct literal, because the `{` would start
the body. Name it first, or wrap it in parentheses.

### What the loop advances

`for` puts the iterator in a slot of its own and advances that, which is what
a binding would have got: a struct iterator is **copied**, so the one you named
is untouched afterwards; a class iterator is **shared**, so it is left spent.
Neither is special to `for` — both follow from what the type already means.

The loop also keeps whatever it is walking alive for as long as the walk lasts,
so `for v in makeVector() { ... }` is safe.

### Destructuring

The loop binding is a pattern, so a value can arrive already taken apart:

```rune
for (x, y) in cells { ... }
for Pair { key, value } in entries { ... }
for ((a, b), name) in nested { ... }
for _ in anything { ... }
```

The pattern has to match every value, because every turn has to bind. A
pattern that could fail — an enum variant, a literal — is refused; take the
value apart with `match` inside the body instead.

### Reshaping one

Every `Iterator` — and every `Sequence`, which hands one out first — carries
the adaptors. Each wraps an iterator in another iterator:

| Written | Yields |
|---|---|
| `.map(f)` | every value with `f` applied to it |
| `.filter(keep)` | only the values `keep` says yes to |
| `.zip(other)` | pairs, ending as soon as either side does |
| `.take_while(keep)` | the values up to the first `false`, and no further |
| `.skip_while(drop)` | everything from the first `false` onwards |
| `.take(n)` / `.skip(n)` | at most `n`, or all but the first `n` |
| `.enumerate()` | each value paired with its position |
| `.chain(other)` | this one's values, then the other's |
| `.as_iter()` | a cursor — `iterate` on a `Sequence`, itself on an `Iterator` |

```rune
for name in people.filter(||(p: Person) -> bool { p.age >= 18 })
                  .map(||(p: Person) -> String { p.name }) {
    io::println(name)
}
```

They compute nothing on their own. A value moves through the chain only when
the loop at the end asks for it, so a chain over a million elements allocates
nothing and walks the source once. `zip` ends as soon as either side does, and
does not advance the longer one past the pair it produced.

`take_while` ends at the first value its test rejects and never asks the source
again — the source is advanced exactly once past the last value taken. That is
the difference from `filter`, which skips a value it does not want and carries
on, and it is what lets a `take_while` sit in front of an iterator that never
ends:

```rune
// [1, 2, 3, 10, 4]
v.take_while(||(n: i64) -> bool { n < 5 }).collect()   // [1, 2, 3] — stops at 10
v.filter(||(n: i64) -> bool { n < 5 }).collect()       // [1, 2, 3, 4] — skips it

iter::counting(1).take_while(||(n: i64) -> bool { n * n < 50 }).collect()
```

### Ending a chain

The adaptors are lazy, so something has to ask:

| Written | Hands back |
|---|---|
| `.collect()` | everything left, in a `vector::Vector` |
| `.count()` | how many are left; walks to the end |
| `.find(keep)` | the first match, or `nil`; stops there |
| `.any(keep)` / `.all(keep)` | stopping at the first yes, or the first no |

```rune
let adults = people.filter(||(p: Person) -> bool { p.age >= 18 }).collect()
let anyMinor = people.any(||(p: Person) -> bool { p.age < 18 })
```

`vector::collect(it)` is the same thing written the other way round, and still
there: the method is what a chain reads better with, and the free function is
what `std::iter` is written against, because a mark cannot depend on a
container that binds it.

### Numbering

`0..n` is loop syntax rather than a value, so `iter::counting(from)` is what
numbers something. It never ends, which is safe because `zip` stops with the
shorter side — and `enumerate` is the same pairing without the second
iterator:

```rune
for (i, name) in iter::counting(1).zip(names.iterate()) { ... }
for (i, name) in names.enumerate() { ... }
```

A mark method may return a type parameterised by `Self` — `map` returns
`Map<Self, B>`, `filter` returns `Filter<Self>`. Every type carrying the mark
gets the method, and the wrapper carries the mark too, so resolving those
results everywhere would never finish. They are resolved where the method is
*called* instead, so `Map<Map<Ticks, i64>, String>` exists exactly when a chain
asks for it. Nothing has to be declared for this; it is how such a signature is
handled.

### In the standard library

| Written | Walks |
|---|---|
| `for v in vec` | a `Vector<T>`, through `VectorIter<T>` |
| `for (k, v) in map` | a `Map<K, V>`, as pairs |
| `for c in text::chars(s)` | the characters of a `String`, in one pass |
| `iter::counting(n)`, `iter::countingBy(n, step)` | integers, endlessly |
| `it.collect()`, `vector::collect(it)` | runs an iterator into a `Vector` |

Bounds name the marks like any other: `fn count<S: Sequence>(s: S)` takes
anything a `for` can walk, and `fn drain<I: Iterator>(it: I)` takes a cursor.
`I::Item` names what a cursor yields, and `S::Iter::Item` what a sequence does.

A `dyn Iterator` cannot be iterated: the mark object does not carry its `Item`
type, so there would be nothing for the loop variable to be.

---

## 9. Option and Result

`Option` and `Result` are ordinary Rune enums, declared in `std::option` and
`std::result`:

```rune
pub enum Option<T> { None, Some(T) }
pub enum Result<T, E> { Ok(T), Err(E) }
```

The compiler knows them by name, so `T?`, `nil`, `??` and `?` are sugar over
them rather than separate machinery. Both types and their variants are in
scope everywhere — no import is needed to write `Some(x)`, `None`, `Ok(v)` or
`Err(e)`.

```rune
let maybe: i64? = nil        // exactly Option<i64>, and `nil` is Option::None
let value = maybe ?? -1      // default when empty
let value = maybe.or(-1)     // same thing, as a method
let present = maybe.hasValue()
let forced = maybe.unwrap()  // aborts when empty

match maybe {
    Some(v) => io::println(v),
    None => io::println("nothing"),
}
```

`?` returns early from the enclosing function when the value is `None` or
`Err`. It works on both, and the enclosing result type has to be the same
shape:

```rune
fn doubled(values: [5:i64]) -> i64? {
    let found = firstEven(values)?      // returns None from here
    found * 2
}

fn addAll(a: String, b: String) -> Result<i64, String> {
    Ok(parse(a)? + parse(b)?)           // the Err travels out unchanged
}
```

The error travels out as the function's *own* error type. When the operand's
differs, `?` looks for a conversion — a `bind Theirs into Ours` — and calls
its `convert` on the way out, so a layered program does not need a `mapErr`
at every boundary. An error type that converts implicitly (a narrow integer
into a wide one) is simply widened.

```rune
enum AppError { Parse(ParseError), Disk(io::FileError) }

bind ParseError into AppError {
    fn convert(&self) -> AppError { AppError::Parse(*self) }
}
bind io::FileError into AppError {
    fn convert(&self) -> AppError { AppError::Disk(*self) }
}

fn run(text: String) -> Result<i64, AppError> {
    let n = parse(text)?                        // ParseError becomes AppError
    let body = io::readToString("data.txt")?    // and so does FileError
    n + body.$length()
}
```

Neither is only a container to be unwrapped. Both carry the whole ordinary
API, and it is worth reaching for, because a chain of these is one calculation
where the unwrapping version is four nil checks:

```rune
// Option: hasValue, isNil, isSuchThat, or, orElse, unwrap, expect,
//         map, mapOr, andThen, filter, otherwise, zip, take, replace, clear
let width = header.map(||(h: Row) -> i64 { h.columns }) ?? 80
let port   = config.andThen(||(c: Config) -> i64? { c.port })
let adult  = age.filter(||(n: i64) -> bool { n >= 18 })
let found  = fromFlag.otherwise(fromEnv).otherwise(fromFile)

// Result: isOk, isErr, isSuchThat, or, orElse, recover, unwrap, expect,
//         unwrapErr, map, mapErr, andThen, ok, error
let text = readFile(path).recover(||(e: io::FileError) -> String { "" })
store(path).mapErr(||(e: io::FileError) -> SaveError { SaveError::Disk(e) })
```

`take` is the one worth knowing about specifically: it hands the value over
and leaves the option empty, which is how a field that *owns* something is
moved out of without the owner being left holding a copy.

```rune
match self.pending.take() {
    Some(job) => run(job),
    None => (),
}
```

At module level, `option::some`, `option::none`, `option::when`,
`option::flatten`, `result::ok`, `result::err`, `result::from` — which turns a
`nil` into a named failure — and `result::flatten`.

They are all plain Rune methods, so reading `stdlib/std/option.rune` shows
exactly what each one does and what it costs.

---

## 10. `.`, `::` and `$`

Two separators, and the rule is about what is on the *left*.

`::` walks a path through things the compiler knows by name — modules, types,
marks, enums:

```rune
io::println(x)          // a function in a module
math::PI                // a global in a module
Shape::Circle(2.0)      // a variant of an enum
Sheep::new("dolly")     // a method with no `self`, on a type
Animal::new("dolly")    // a static requirement, on a mark
```

`.` reaches into a value you already have:

```rune
point.x                 // a field
point.distance()        // a method
pair.0                  // a tuple element
```

`value::Mark.name()` is the one place both appear: `::` picks the mark, `.`
reaches the method through it.

A `$` before a member name asks for one the *compiler* provides rather than one
the type declares. The two namespaces are separate, so a type may have its own
`length` or `str` without either shadowing the other:

```rune
text.$length()          // the compiler's, on any String
tag.length()            // the type's own
```

## 11. Builtin methods

`io::Display` is the mark that decides how a value prints. Every builtin is
bound to it, and `io::print`, `io::println` and `io::debug` take any `T:
Display`, so `io::println(42)` and `io::println(myPoint)` are the same call:

```rune
bind io::Display to Point {
    fn display(&self) -> String { "(" + self.x.$str() + ", " + self.y.$str() + ")" }
    fn describe(&self) -> String { "Point" + self.display() }   // for io::debug
}
```

On any primitive, `.$str()` renders it as a `String`, which is what makes
string building with `+` practical inside a `display` implementation.

Every one of these carries a `$`, which is what keeps the compiler's names out
of the way of your own:

`String`: `$length` `$charCount` `$isEmpty` `$at` `$byteAt` `$substring`
`$find` `$repeat` `$toInt` `$toFloat` `$cstr` `$hash`.
Arrays and slices: `$length` `$isEmpty`.

`unwrap`, `hasValue` and `or` on an `Option` are ordinary library methods
declared in `std::option`, so they take no sigil.

`String` cannot be indexed with `[]` — UTF-8 is variable width, so use `.$at(i)`
for a `Character` or `.$byteAt(i)` for a raw byte. `$at` counts characters from
the start each time; to walk the whole string, use `for c in text::chars(s)`,
which walks bytes and is linear.

---

## 12. Modules and visibility

### Reaching the standard library

A fully qualified path needs no import at all — `std::io::println("hi")` names
exactly one thing whether or not the file mentioned it:

```rune
fn main() -> i64 {
    std::io::println("no import at all")
    0
}
```

`import` is for the short names. `std` is a *prefix* rather than a module, and
importing a prefix brings in the modules beneath it:

| Form | Brings into scope |
|---|---|
| `import std` | `io`, `mem`, `math`, `process`, `text`, `testing`, … |
| `import std::*` | the same — a prefix import is already a glob |
| `import std::{io, mem}` | just those two |
| `import std::io` | `io` |
| `import std::io::*` | `println` and the rest, unqualified |

A name inside `{...}` has to be a module directly beneath the prefix.
`std::collections` is itself a prefix, so `import std::{io, collections}` is
refused — the diagnostic names what lives under it, and
`import std::collections::vector` is the spelling that works.

One file is one module. A package's `src/main.rune` or `src/lib.rune` *is* the
package module; every other file under `src/` becomes a submodule of it, so
`geometry` and `geometry::shapes` never collide with another package's
`shapes`.

```rune
import std::io                  // binds `io`
import std::io as console       // binds `console`
import std::{io, math}          // binds both directly
import std::math::*             // brings every public item into scope
```

Everything is private unless marked `pub`. There are no headers: the compiler
reads the declarations it needs from source or from a `.rul` library.

---

## 13. Foreign interfaces

```rune
extern "C" {
    printf(format: CString, ...) -> i32     // `fn` is optional here
    fn abs(value: i32) -> i32
    var errno: i32
}
```

Foreign calls are unsafe operations. Variadic arguments follow the C
promotions, so `f32` widens to `double` and narrow integers widen to `int`.

---

## 14. Decorators

```rune
@unsafe                    // marks a function unsafe; callers are warned
@safe("reason")            // justifies unsafe use inside; silences the warning
@inline                    // always inline
@noinline
@export("c_name")          // fixed symbol name, no mangling
@alias("other")            // a second name for the declaration
@as("cBind")               // inside `extern` only: renames it for Rune's side
@resource                  // on a field: `deinit` has to release it
@intrinsic("name")         // the compiler answers it directly
@sync("reason")            // a class that synchronises its own access
@Doc("...")                // prose the compiler keeps; what `rune doc` reads
@link("m") @linkpath(...)  // on a file: native libraries it needs
@type(Library)             // on a file: what it produces
```

Decorators precede a declaration and may take arguments. A name that is
neither one of these nor a decorator the program declared is diagnosed rather
than ignored.

### `@alias` and `@as`

`@alias` **adds** a name — the declaration answers to both. `@as` **replaces**
one, and only inside an `extern` block: the C library goes on exporting the
name that was written, and Rune sees the new one. That is what lets a program
import C's `bind` and still declare a `bind` of its own.

```rune
extern "C" {
    @as("cAbs")
    fn abs(v: i32) -> i32       // links against `abs`; called `cAbs` here
}

fn abs(v: i64) -> i64 { if v < 0 { 0 - v } else { v } }
```

Written on anything that is not an `extern` declaration, `@as` would silently
do nothing, so it is refused.

### `@resource`

A field marked `@resource` holds something reference counting cannot release
— a descriptor, a handle, a lock. Saying so makes the type's `deinit`
responsible for it: at `--safety full` a `deinit` that never mentions the field
is an error, and so is having no `deinit` at all. Below `full`, both are
warnings. See §16.

```rune
pub struct Socket {
    @resource fd: i32 = -1,
    pub port: i32 = 0
}

extend Socket {
    @safe("the descriptor is ours, and the flag stops a second close")
    fn deinit(&var self) {
        if self.fd >= 0 { close(self.fd); self.fd = -1 }
    }
}
```

---

## 15. Raw memory: `std::mem` and `std::collections`

Most code never needs any of this. `Handle<T>` is the safe layer, the allocator
is the unsafe one, and `Vector<T>` is what the unsafe one is for.

```rune
import std::mem

mem::size_of<i64>()          // 8
mem::align_of<i64>()         // 8

var h = mem::Handle<i64>(41) // one value on the heap, reference counted
h.set(h.get() + 1)
let g = mem::of("inferred")  // same thing, type inferred
```

`Handle<T>` is a class, so copies share the value and the last reference
releases it. Nothing about using one is unsafe.

Under it sits `Allocator`, a mark, with `mem::allocator` bound to the process
heap:

```rune
let block = mem::allocator.allocate(4 as usize * mem::size_of<i64>())
let cells = unsafe { block as *var i64 }
unsafe { cells[0] = 100 }
mem::allocator.deallocate(block)
```

`p[n]` on a raw pointer is offset arithmetic with nothing to check against —
the one indexing form that is never bounds checked, and the reason it needs an
unsafe context. A store through a raw pointer is raw: no reference counting
happens, which is what `mem::retain` and `mem::release` are for when the values
stored are counted ones.

`std::collections::vector` is the worked example. `Vector<T>` grows
by doubling, hands back `Option` where an index might not exist, and balances
the reference counts of whatever it holds:

```rune
import std::collections::vector

var v = vector::Vector<i64>()
v.push(1)
v.push(2)
v.get(0)            // 1, aborts if out of range
v.at(99)            // nil
v.pop()             // Some(2)
let names = vector::from(["ada", "grace"])
let view = names.asSlice()   // a [String] over the same storage, no copy
```

`asSlice` is the bridge to everything written for slices — `std::collections::slice`,
slice patterns, a function taking `[T]`. The slice is a view: valid while
the vector lives and until the next `push`, `reserve` or `clear`, which may
move the storage. Underneath is `mem::slice_of<T>(block, count)`, the one
unsafe intrinsic that makes a slice from an address and a count, for any
container of your own to do the same.

### `Buffer<T>`: a fixed-size checked block

Where `Vector<T>` grows, `Buffer<T>` does not: it takes its size once and keeps
it. Every read and write is compared against that size, so a buffer is a safe
way to hold a block of `T` without touching the allocator yourself.

```rune
import std::mem

var b = mem::Buffer<i64>(size: 4, fill: 0)   // four slots, every one set to 0
b.length()                          // 4
b[0] = 10                           // subscript assignment
b[1] += 20
b.at(9)                             // nil — out of range, no panic
b.read(0)                           // 10 — in range, whatever its bytes are
b.get(0)                            // 10, panics if out of range
b.put(2, 30)                        // false if out of range, no panic
b.holds(3)                          // true
b.fill(-1)                          // every slot
```

`at` also answers `nil` for a slot nothing has been written to, which it works
out from the slot's bytes being all zero. That is right for a slot nobody has
touched and wrong for one holding a value whose bytes happen to all be zero —
an empty `String`, or the first variant of a counted enum that carries
nothing. `read` is the form for a caller that keeps its own record of which
slots are live, as `Map` does; it asks only whether the index is in range.

The constructor takes a fill value as well as a size, and that is deliberate: a
slot that had never been written would hand back whatever the allocator left
there, so there is no way to make an uninitialised buffer.

Three access styles sit side by side. `at` returns `T?` and never fails; `get`
and `b[i]` panic on a bad index; `put` returns `bool` so a caller can decide.
`b[i] = v` goes through `put` and panics on failure, which is what the
subscript form of an assignment means everywhere in Rune. Counted elements are
retained on the way in and released when overwritten or when the buffer dies.

### Files: `std::io`

`std::io` reads and writes files without exposing a file descriptor. `File` is
a class, so the handle closes when the last reference to it goes — but `close`
is there when the moment matters.

```rune
import std::io

match io::writeString("/tmp/notes.txt", "first\nsecond\n") {
    Ok(n)  => io::println("wrote " + n.$str() + " bytes"),
    Err(e) => io::println("failed: " + io::describe(e)),
}

match io::open("/tmp/notes.txt") {
    Ok(f) => {
        while f.readLine() is Some(line) { io::println(line) }
        f.close()
    }
    Err(e) => io::println(io::describe(e)),
}

io::appendString("/tmp/notes.txt", "third\n")
io::readToString("/tmp/notes.txt")     // Result<String, FileError>
io::exists("/tmp/notes.txt")           // bool
io::delete("/tmp/notes.txt")           // FileError? — nil on success
```

Every operation that can fail returns `Result<T, FileError>`, so `?` chains
them, and `FileError` is an enum — `NotFound`, `PermissionDenied`,
`AlreadyExists`, `IsDirectory`, `TooManyOpenFiles`, `Closed`, `Other` — that
`match` can take apart. `io::describe` turns one into a sentence.

`open` reads, `create` truncates, `append` writes at the end, and `openWith`
takes the mode string if you need one the three do not cover. On a `File`:
`readLine() -> String?`, `readAll() -> String`, `write(text)`, `writeLine(text)`,
`flush()`, `close()`, `isOpen()` and `name()`.

Standard input has two spellings, and the difference matters: `io::readLine()`
gives a `String`, and an empty one is what a blank line *and* the end of the
input both give. `io::readLineOrEnd()` gives a `String?` and is the one to
loop over:

```rune
while io::readLineOrEnd() is Some(line) { io::println(line) }
```

### Bytes and streams: `std::io`

A `String` is text — UTF-8, NUL-terminated for the C boundary — which makes it
the wrong thing to read a socket into, because what arrived is whatever the
other end sent. `Bytes` is a growable run of bytes, and it is what the stream
marks move:

```rune
var b = io::Bytes()
b.appendText("GET / HTTP/1.1\r\n")
b.length()              // i64
b.at(0)                 // u8?
b.slice(0, 3)           // Bytes
b.find("HTTP", 0)       // i64, or -1
b.consume(4)            // drop the first four, move the rest to the front
b.toString()            // read the contents as text
b.raw()                 // *var u8, for the C boundary
```

Two marks describe where bytes come from and where they go, and a third is
both:

```rune
mark Reader {
    fn read(&var self, sink: Bytes, most: i64) -> Result<i64, StreamError>
    // defaults: readAll, readExact
}

mark Writer {
    fn write(&var self, data: Bytes) -> Result<i64, StreamError>
    fn flush(&var self) -> Result<i64, StreamError>
    // defaults: writeAll, writeText, writeLine
}

mark Stream: Reader + Writer {}
```

`read` returning `0` means the source is finished; a source with nothing ready
*yet* reports `StreamError::WouldBlock` instead, so the two cases never have to
be told apart by guessing. `BufferedReader<R: Reader>` puts a buffer in front
of one so `readLine` costs one system call rather than one per byte, and it
keeps whatever arrived past the end of the line it handed back — which is what
makes it usable on a socket. `io::copy(source, target)` moves everything from
one to the other.

`File` has a `write` and a `flush` of its own, taking text and reporting a
`FileError`, and Rune has one name per method — so a file is *turned into* a
stream rather than being one:

```rune
var out = file.asStream()       // io::FileStream, which binds Stream
out.writeLine("alpha")

var lines = io::BufferedReader<io::FileStream>(file.asStream())
while lines.readLine() is Some(line) { io::println(line) }
```

### TCP: `std::net`

`listen` and `connect` are the two ways in, and what comes back is an
`io::Stream` — so `readLine`, `writeText`, `BufferedReader` and `io::copy` all
work over a socket without knowing it is one.

```rune
import std::{io, net}

var listener = match net::listen("127.0.0.1", 8080) {
    Ok(l) => l,
    Err(e) => { io::eprintln(net::describe(e)); return 1 },
}
io::println("listening on " + listener.port().$str())   // port 0 picks one

match listener.accept() {
    Ok(c) => {
        var conn = c
        conn.setNoDelay(true)
        conn.setReadTimeout(5000)
        var lines = io::BufferedReader<net::TcpStream>(conn)
        match lines.readLine() {
            Some(request) => lines.writeText("HTTP/1.1 200 OK\r\n\r\n"),
            None => {},
        }
    },
    Err(e) => io::eprintln(net::describe(e)),
}
```

`TcpStream` and `TcpListener` are structs that own their descriptor: the field
is `@resource`, the `deinit` closes it, and handing one on is a move (§16). So
a connection closes itself when the binding holding it goes out of scope, and
there is never a second owner to close it twice.

That also means one must not go into a container or through a channel — both
store what they carry through memory the compiler cannot follow, so there
would be two copies of one descriptor and nothing to say which owns it.
`release` hands the descriptor out as a plain `i64`, which nothing owns and
nothing can close, and `adopt` takes it back:

```rune
queue.send(conn.release())          // on the accepting thread
var conn = net::adopt(descriptor)   // on the worker
```

`examples/project/webserver` is a worker pool built that way. `NetError` names
the cases worth telling apart — `Refused`, `AddressInUse`, `Unreachable`,
`WouldBlock`, `Interrupted`, `TimedOut`, `Reset`, `PermissionDenied`,
`NotFound`, `Closed`, `Failed` — and `net::describe` turns one into a sentence.

### The everyday modules: `env`, `random`, `hash`, `json`, `cli`, and dates

The things an ordinary program wants from outside itself, each a module of
its own.

```rune
import std::{env, random, hash, json, cli, time}

let editor = env::getOr("EDITOR", "vi")          // nil when unset, "" when empty
env::set("GREETING", "hello")

var rng = random::new()                           // seeded from the OS
let roll = rng.between(1, 6)
var fixed = random::seeded(42)                    // repeats exactly
let token = random::bytes(16)                     // straight from the OS: for secrets

hash::fnv1a64("key")                              // a named hash, the same everywhere
hash::crc32("payload")
hash::sha256("hello")                             // 64 hex characters

let doc = json::parse(text)?                      // Result<Value, Error>, with line and column
doc["user"]["tags"][0]                            // null for what is not there
json::pretty(json!{ "name": "ada", "tags": ["x"] })

let today = time::today()
today.plusDays(30).weekday()                      // a Weekday
time::utcNow().iso()                              // 2026-09-11T10:20:30Z
time::parseDateTime("2026-09-11T11:20:30+01:00")  // read back, zone and all
```

`std::cli` is the command line, declared and then asked:

```rune
var app = cli::Parser("greet", "Prints a greeting.")
app.flag("loud", "l", "shout it")
app.option("name", "n", "who to greet", "world")
app.positional("times", "how many times")
let args = app.parseOrExit()                      // --help and mistakes handled
if args.has("loud") { ... }
let name = args.value("name") ?? "world"
```

`--name value`, `--name=value`, `-n value`, `-nvalue`, `-lv` and `--` all
read the way they usually do, and `--help` prints a page built from the
descriptions.

---

## 16. Memory and safety

Value types are copied. Classes, `String`, closure environments and mark
objects are reference counted, retained on copy and released at scope exit,
with `deinit` running when the count reaches zero. Destruction is deterministic
and chains from a subclass into its base.

### Values have destructors too

A struct or an enum may declare a `deinit`, and it runs when the value it
lives in is destroyed rather than when a count reaches zero. That is what lets
a value own something reference counting cannot see — a file descriptor, a
lock, a handle from C. It may be written in the type's body, in an `extend`, or
supplied by a `bind`; all three are the same method to the compiler.

```rune
struct Handle { pub id: i64 }

extend Handle {
    // `&var self`, not `self`: by value it would run on a copy and leave the
    // original holding what it was meant to release.
    fn deinit(&var self) { close(self.id) }
}
```

It runs from wherever the value is: a local at the end of its scope, a
statement whose value is thrown away, and anything *containing* one — a field
of a struct or a class, an element of an array, a member of a tuple. A class
runs its own `deinit`, then destroys its fields, so a struct field with a
destructor is reached that way.

Because only one thing may own a resource, handing such a value on is a
**move**: returning it, passing it by value, or storing it somewhere that
outlives the binding. The binding it came from stops owning it and stops being
usable.

```rune
fn consume(h: Handle) { }

let a = Handle { id: 2 }
consume(a)
a.id                    // error: 'a' has been moved out of
```

Borrowing takes nothing, so `borrow(&a)` leaves the caller owning it. Move
tracking is per binding and textual rather than a walk of every path; what
that misses — a move inside a loop that runs twice — is safe at run time,
because the binding carries a flag saying whether it still owns anything and a
destructor never runs twice.

A field marked `@resource` says the type's `deinit` is responsible for it; see
§14.

### Weak references

A `weak` field does not keep its target alive, and reads as empty once the
target is gone — the runtime empties every registered slot during destruction,
so a weak reference can never dangle. That is how a cycle is broken:

```rune
class Parent {
    pub child: Child?      // strong: the parent owns the child
}

class Child {
    weak parent: Parent    // weak: the child only observes
}

match child.parent {
    Some(p) => io::println(p.name),
    None => io::println("the parent is gone"),
}
```

A weak field must refer to a class, and reading one always produces an
`Option`, because "still alive?" is a question that has to be answered
somewhere.

### `Unique`: one owner, so no ring

`weak` breaks a cycle after the fact — you have to see it coming and pick an
edge. `Unique` makes one impossible instead.

A `Unique<T>` is a reference to a class instance with **exactly one owner**. It is
represented like any other class reference and costs nothing extra at run time;
what makes it unique is that the compiler will not duplicate it. **Moving is what assignment and `return` already do.** Putting a `Unique` into
another `Unique` slot, or returning one, hands it over — the source stops
naming anything. Borrowing with `&` takes nothing away. There is no third
thing, and `.$move()` says the same as an assignment for the cases where
neither applies. Closing
a ring needs a second reference to the same object, and no one can produce one,
so a structure owned through `Unique` is acyclic by construction rather than by
inspection.

```rune
class Node {
    pub value: i64
    pub next: Unique<Node>?           // this node owns the rest of the chain
    fn init(self, value: i64) { self.value = value }
}

let head:   Unique<Node> = Node(1)
let second: Unique<Node> = Node(2)
head.next = second            // `second` is unusable from here on
```

Reading is borrowing. A borrow reaches the object without becoming a second
owner, so it cannot be stored anywhere that would form a ring:

```rune
fn total(n: &Node) -> i64 {
    var sum = n.value
    match n.next {
        Some(c) => sum += total(&c),
        None => {}
    }
    sum
}
```

Four things are refused, all as `E0239`:

```rune
let a: Unique<Node> = Node(1)
let b: Unique<Node> = Node(2)
a.next = b
io::println(b.value.$str())   // 'b' has been moved out of
let c: Unique<Node> = Node(3)
let d: Unique<Node> = c          // cannot copy into this initialiser
let e: Node = c               // cannot copy into a counted reference
a.sneaky = c                  // ... which is what would have made the ring
```

Returning one is a transfer, so it is written out:

```rune
fn buildChain(count: i64) -> Unique<Node> {
    var head: Unique<Node> = Node(count - 1)
    var i = count - 2
    while i >= 0 {
        let node: Unique<Node> = Node(i)
        node.next = head      // the new node takes the chain over
        head = node           // and becomes the chain
        i -= 1
    }
    head
}
```

A chain is built from the tail forwards because each node owns the one after
it: there is no way to keep a cursor on the end and still hand the whole thing
back. Assigning to a local that was moved out of gives it something to hold
again, which is what makes the loop above work.

Three limits worth knowing:

* **`Unique` cannot be an explicit generic argument.** `Option<Unique<Node>>` is
  fine — it is what `Unique<Node>?` means, and `Option` is the compiler's own —
  but `Handle<Unique<Node>>` is refused. A generic written for a copyable `T`
  would copy this one.
* **Move tracking is flow-insensitive.** A move inside one branch of an `if`
  marks the local moved for everything after the `if`, even on the path that
  did not take it. That refuses some valid programs; it never accepts an
  invalid one.
* **Moving out of a field is not allowed.** The field would be left holding
  nothing, and only an optional field can say that. Assign a replacement.

`--safety` controls the checks the compiler inserts:

| Level | Bounds | Nil | Divide by zero | Leak report |
|---|---|---|---|---|
| `none` | — | — | — | — |
| `minimal` | — | ✓ | — | — |
| `full` (default) | ✓ | ✓ | ✓ | ✓ |

Unsafe operations — raw pointer dereference, pointer casts, foreign calls, and
calls to `@unsafe` functions — warn in a safe context. Silence one by marking
the caller `@unsafe`, justifying it with `@safe("reason")`, or wrapping the
expression in `unsafe { }`.

### Borrows and ownership

`full` also reads each function body for what it does with what it owns. This
is a whole-body pass rather than a run-time check, so it costs nothing at run
time and reports before the program is built. Below `full` each finding is a
warning instead.

| Reported | Because |
|---|---|
| A borrow returned from the function it points into | the binding is destroyed on the way out, so the caller would be handed an address to nothing |
| Two live borrows of one place, one of them able to write | a writer has to be the only one |
| A value that owns a resource read out of a field by value | that would make a second owner, and the resource would be handed back twice |
| A use of a binding that has been handed away | it no longer refers to anything |

```rune
fn dangling() -> &i64 {
    let n = 5
    &n                  // error: returns a borrow of 'n', which does not
}                       //        outlive the call

var p = Point { x: 1, y: 2 }
let a = &var p
let b = &p              // error: cannot borrow 'p' while it is borrowed
```

A borrow stops mattering after its **last mention** rather than at the end of
the block, so finishing with one and then reading the value again is fine. A
borrow is followed back to the binding it starts from, so two *fields* of the
same value count as the same place — following an index the compiler cannot
evaluate would report the same conflicts with less certainty, so it does not
try, and the diagnostic says as much.

The check is function-local. A borrow handed to a call is the callee's
business for the length of the call, and a raw pointer is not followed at all,
which is what `unsafe` means.

### Counting that is not emitted

The same pass answers a question nothing reports: which locals never leave the
scope that declared them. A class built for one of those needs no counting —
nothing else can reach it, so the allocation's own reference *is* the
binding's, and the scope hands it back on the way out. The retain, the
temporary slot and the paired release are simply not emitted.

Nothing has to be written to get this and nothing observable changes;
`process::liveObjectCount()` agrees either way. Anything the pass cannot
follow escapes — a capture, a raw-pointer cast, a store into a field, a call
taking the value — so it applies where the whole story is visible in one body
and nowhere else.

Reference cycles are not collected, and there is no garbage collector to find
them. Rune offers three answers instead, in increasing order of strength:
`weak`, which breaks a ring you can see; `Unique`, which makes one impossible;
and the leak report at `--safety full`, which counts whatever is still alive
when the program ends.

A class that *can* reach itself through counted references is reported as
`W0235`, with the route spelled out. It is a warning, not a refusal: the check
works on types, and a type that can loop is not a program that does — a plain
singly linked list would otherwise be rejected. Where a structure should be
incapable of looping at all, own it through `Unique` and the question does not
arise.

### `--memory zombie`: single ownership, no count

Reference counting is the default, not the only choice. Built with
`--memory zombie` (or `memory = "zombie"` under `[build]`), a program keeps no
counts at all. Every value that the heap owns — a class, a `String`, a closure,
a mark object — has **exactly one owner**; handing it on *moves* it and leaves
the source empty, and it is destroyed the moment its owner's scope ends. A
second, precise borrow checker — **Zombie** — proves that every borrow is gone
before the value it points at is, so nothing dangles and nothing is freed
twice. `--memory arc` (the default) is reference counting, unchanged; a library
records which mode it was built for, and mixing the two is refused.

```rune
class Box { var v: i64  fn init(self, v: i64) { self.v = v } }
fn take(b: Box) -> i64 { b.v }

let x = Box(1)
let a = take(x)         // `x` moves into `take`
let b = take(x)         // error: 'x' has been moved out of
```

`&x` and `&var x` borrow without owning, and `$clone()` asks for an independent
copy where one is meant. Zombie's findings are always errors, at every
`--safety` level, because the generated code has no count to fall back on: the
checker's verdict is what makes it sound.

The checker is flow-sensitive and place-based, after the model of Rust's
Polonius. A borrow lasts until its **last use**, not to the end of the block;
two borrows of *different* fields never clash; and a `&var self` method may
read its receiver while its arguments are worked out (`c.add(c.count())`),
because the receiver is reserved when the call is written and exclusive only
when it runs. A reference conditionally returned from one branch and rebuilt in
another — the case NLL rejects — is accepted.

Where a returned reference is borrowed from is **inferred from the body**, so
most functions carry no annotation. When the boundary should be written down —
to pin a public interface, or where a result could come from more than one
argument — a `from` clause names the place, as a plain path rather than an
invented lifetime name:

```rune
fn longest(a: &String, b: &String) -> &String from (a, b)   // borrows from either
fn first(&self) -> &Item from self.items                    // from a field of self
fn banner() -> &String from global                          // from a global
```

Two more spellings follow from the same idea. A **view** on a `&var self`
method, `fn bump(&var self { counter })`, promises it touches only those
fields, so a caller may hold a borrow of another field across the call; views
are inferred and written only to pin them. And an **internal reference**,
`body: &String from self.text`, lets a struct field borrow from another field
of the same value — the struct still owns everything and can be moved, because
moving it moves the handle, not the borrowed data.

Two things that need a count are gone under Zombie: a `weak` field (which
cannot know its target is freed without one) and the types that exist to be
shared (`thread::Arc`, `mem::retain`/`release`), each reported with the
alternative to reach for. `@zombie("reason")` trusts a body the checker cannot
follow, and `unsafe { }` leaves raw pointers untracked, as always.

---

## 16b. What a file produces

`@type` at the very top of a file — before `@link` and `@linkpath`, because
what a file produces decides how the rest are used:

```rune
@type(Object)      // Executable | Library | Object | Assembly | LLVM
@link("m")         // short forms: Exec, Lib, Obj, Asm
```

A file with a `main` and no `@type` is an executable. A flag on the command
line still wins. An object has no entry point, so nothing runs global
initialisers for it — anything constant in such a file has to be a function.

### Every root in a package is built

Inside a package, `rune` reads what each file under `src/` says it produces and
gives each one its own target:

| A file in `src/` | What it becomes |
|---|---|
| `main.rune`, or any file with a top-level `main` | an executable, named after the file |
| `lib.rune`, or `@type(Library)` | a `.rul` library |
| `@type(Object)` / `@type(Assembly)` / `@type(LLVM)` | `.o` / `.s` / `.ll` |
| anything else | a **component** — importable, compiled into whatever names it |

So a package with `src/main.rune`, `src/report.rune` (which has its own `main`)
and `src/tool.rune` (marked `@type(Executable)`) builds three separate binaries,
and `src/shared.rune`, which declares nothing, is compiled into each of them.
`src/main.rune` is the one `rune run` means when you do not name a target, and
its binary is named after the package rather than the file. Name one with
`rune run report`, or run every one with `rune run --all`.

Every target in a package is compiled under the **package's** module name, not
the file's, so `import mypkg::shared` means the same thing whichever target is
being built.

A `@type` inside a comment does not count — the scan strips comments first.

### Building the whole package as something else

`@type` says what one file produces. `rune build --emit <kind>` says it for
every root the package owns at once, producing one file per root under
`target/<profile>/` and linking nothing:

```
rune build --emit llvm-ir     # target/debug/<name>.ll, per root
rune build --emit asm         # .s
rune build --emit obj         # .o
rune build --emit lib         # .rul, even for a package with a main
rune build --emit exe         # the default
```

`[build] emit = "llvm-ir"` in the manifest says the same thing when the
command line does not; the command line wins.

Dependencies still build as libraries whatever `--emit` says, because a `.rul`
is what this package needs from them in order to compile at all. A package
that also produces a library keeps producing it: the emitted file is the same
code in another form, not a replacement for what a dependent links against.

## 16c. Testing

A test is an ordinary program. `rune test` builds and runs every file under
`tests/`, and the exit status is the verdict — which is what `std::testing`
produces for you. There is no runner to configure and no attribute to remember.

```rune
import std::testing
import mypkg

fn main() -> i64 {
    testing::equal("add sums its arguments", mypkg::add(2, 2), 4)
    testing::isTrue("a fresh buffer holds its size", mem::Buffer<i64>(4).holds(3))
    testing::isNone::<String>("nothing written yet", mem::Buffer<String>(2).at(0))
    testing::summary()
}
```

Each check prints its own line as it runs, so a failure names itself and says
what it expected:

```
  ✓ add sums its arguments
  ✗ this one fails on purpose
      expected 5, got 4
  1 of 3 failed
```

| Check | Passes when |
|---|---|
| `equal(name, got, want)` | the two render the same |
| `notEqual(name, got, want)` | they do not |
| `isTrue(name, cond)` | the condition holds |
| `isFalse(name, cond)` | it does not |
| `isSome(name, opt)` | the Option holds something |
| `isNone(name, opt)` | it holds nothing |
| `unreachable(name)` | never — for a branch that should not have run |
| `counts()` | — returns `(passed, failed)` |
| `summary()` | — prints the tally, returns 0 when all passed, 1 otherwise |

`equal` compares values **as they print**. That covers every builtin exactly,
and it means a type only has to be bound to `io::Display` to be testable —
there is no separate equality bound to satisfy, which matters because a builtin
like `i64` has built-in `==` rather than a binding to `operator::eq`.

Each file is compiled as its own program and linked against the package, so a
test can import the library it is testing by name. The summary line counts
*files*; the per-check tally is printed by each file as it runs. A test that
aborts — a failed bounds check, an explicit `process::panic` — is a failure
like any other, because the exit status is non-zero.

`rune new` scaffolds `tests/basics.rune` already written this way, so a fresh
package has a passing test before you have written any code.

### What goes where in generated docs

What a declaration *is* decides where its definition appears.

| Kind | Where its definition goes |
|---|---|
| `enum`, `struct` | in full on its own page, under the title |
| `fn`, `class`, `mark`, `extend` | in `appendix.md`, one page holding them all; the declaration's page links there |

The split is between a declaration that carries data and one that is only code.
An enum is its variants and a struct is its fields, so showing them elsewhere
would send the reader away from the page they are on. A class or a mark is a
wall of signatures, and its page is better spent on prose and a table of
methods.

## 16c2. Collections and dictionaries

`std::collections` holds three ways to keep a run of values, and
`std::dictionary` holds keyed lookup.

| Holding | Written | Length | Owns its storage |
|---|---|---|---|
| Array | `[5:i64]` | part of the type, constant | yes, inline |
| Slice | `[i64]` | carried at run time | no — points at someone else's |
| Vector | `vector::Vector<i64>` | grows as you push | yes, on the heap |

An array converts to a slice on its own, so `std::collections::slice` covers
both: `at`, `first`, `last`, `indexWhere`, `firstWhere`, `anyOf`, `allOf`,
`countWhere`, `indexOf`, `contains`, `minimumBy`, `maximumBy`, `fold`,
`toVector`, `reversed`, `filtered`, `sortedBy` and `join`. The predicate forms
exist because two values of an arbitrary `T` cannot be compared without knowing
something about `T`; the value forms compare values *as they print*.

Every container subscripts. `v[i]`, `m[key]` and `m[key] = value` are the
direct forms and **abort** when the index or key is not there; `at` is the form
that answers instead. `s[value]` asks a set about membership, and a set has
nothing to store, so it has no `s[value] = ...`. A `Map` iterates as
`(key, value)` pairs and a `Set` as its members. A slice indexes and iterates
natively; `slice::iterate(values)` exists only to reach the `std::iter`
adaptors, which arrive through the `Iterator` mark — and a mark cannot be bound
to a builtin type.

`std::dictionary` has `Map<K, V>` and `Set<T>` — open-addressed hash tables
with linear probing, backed by `mem::Buffer`, doubling past two thirds full.

```rune
var ages = dictionary::Map<String, i64>()
ages.put("ada", 36)
match ages.at("ada") { Some(n) => ..., None => ... }

let seen = dictionary::setOf(["a", "b", "a"])   // 2 members
```

A key is hashed and compared **structurally**, by `mem::hash` and `mem::equals`
— the compiler works both out from the layout, so a key needs nothing of its
own: no `Hashable` mark, and no `Display` standing in for one. Any type at all
can be a key. A `String` matches on contents; a class matches on identity, so
two objects with equal fields are two different keys; an aggregate matches part
by part. `keysOf`,
`valuesOf` and `membersOf` return table order, which is neither insertion order
nor stable across a resize; sort the result when order matters.

## 16d. Namespaces

A package's name is the root of its namespace. `src/lib.rune` or
`src/main.rune` **is** the package module; every other file under `src/` is a
module beneath it, so `src/io.rune` in a package called `app` is `app::io`.
Nothing is flattened, and two packages can both have a `shapes.rune`.

A package may therefore declare a module whose short name the standard library
already uses. `app::io` and `std::io` are different modules, and both are usable
in one file. Three rules decide what a name means:

| Rule | What it means |
|---|---|
| A fully qualified path always works | `std::io::println(...)` and `app::io::render(...)` need no import at all |
| An import binds the **last segment** | `import std::io` and `import app::io` each put `io` in scope |
| The **last import wins** | importing both leaves `io` meaning whichever came second — silently; the other is still reachable by its full path |

Because the last import wins with no warning, a file that imports two modules
sharing a short name should qualify both rather than depend on the order.

A submodule is a module in its own right, not an item inside its parent:
`json::io` resolves even though `json` declares nothing called `io`, and it
resolves the same way whether `json` is a package in this build or a `.rul` on
the search path.

```rune
import json          // the package module
import json::io      // a module beneath it — a separate import
```

A binary compiles under `<package>__bin_<file>` rather than the package's own
name, which is what lets `src/main.rune` say `import <package>` and reach the
package's library instead of finding itself.

### Enum variants share a namespace of their own

`Colour::Red` is the name of a variant. A bare `Red` is a shorthand for it,
and a shorthand is all it is: variant names live beside the names a module
declares rather than among them. So a variable, a function or a type may take
a variant's name, and two enums may each have one:

```rune
enum Car  { Body, Wheel, Door }
enum Ship { Hull, Wheel, Sail }

global Wheel: i64 = 42          // fine; neither enum loses anything
fn Door() -> i64 { 7 }          // also fine
```

What a shared name costs is that a bare mention has to say which enum it
belongs to. The error arrives at the use rather than the declaration, and
lists the candidates:

```rune
let c = Wheel       // error: 'Wheel' names a variant of more than one enum
                    //   note: write which one is meant: Car::Wheel, Ship::Wheel
let c = Car::Wheel  // fine
```

A `match` arm is the exception, because the scrutinee's type already says
which enum is meant:

```rune
fn describe(c: Car) -> String {
    match c {
        Wheel => "car wheel",   // `Car::Wheel`, unambiguously
        Body  => "car body",
        Door  => "car door",
    }
}
```

Lookup order is: the ordinary scope chain first — locals, parameters, globals,
functions, types — then the variants this module declares or imports, then
`Option`'s and `Result`'s. So a binding named `Wheel` shadows the variants, and
a module that declares its own `Some` wins outright over the prelude's without
becoming ambiguous.

## 16e. Branches that produce nothing

Every branch of an `if` or a `match` has to produce the same type — but only
when something is going to *use* that type. In statement position nobody reads
the result, so there is nothing for the arms to agree on:

```rune
// A statement: these arms produce `()`, `i64` and `String`.
if n > 3 { io::println("big") }
elif n > 1 { 1 }
else { "small" }
```

| Where the branching expression sits | Arms must agree |
|---|---|
| a statement on its own | no |
| the tail of a loop body | no — `while` and `for` produce `()`, and a `loop` produces what `break` carries |
| a binding's initialiser | **yes** |
| a function's result | **yes** |
| an operand of something else | **yes** |

The rule follows the value, not the syntax: an `if` nested inside a discarded
one is still checked when *its* value is used.

The other half of the same rule: an `if` with **no `else`** has nothing to
produce when the condition is false, so it is `()` — fine as a statement, never
usable as a value. The error names the `if` rather than whatever was waiting on
it. Reach instead for a `match` over the Option, for `&&` between independent
tests, or for an explicit `else` on every branch.

Tuples convert element by element, so a literal becomes the shape the context
asked for — `(1, "bussin")` is a `(i64, Any)` where one is expected, with the
`String` boxed in place. A member is reached with `.`, like any other:

```rune
let tup: (i64, Any) = (1, "bussin")
tup.1.expect::<String>() == "bussin"
```

## 16f. Macros

A macro is a rewrite from one run of tokens to another, chosen by pattern.
Expansion happens before anything is parsed, so a macro can stand for whatever
the grammar accepts.

```rune
macro twice {
    ($x: expr) => { ($x) + ($x) }
}

macro ints {
    ($($item: expr),*) => {
        {
            let built = vector::Vector<i64>()
            $( built.push($item); )*
            built
        }
    }
}

twice!(3) + 1        // 7
ints!(1, 2, 3)       // a Vector of three
```

A definition is `macro name { (pattern) => { expansion } }` with as many rules
as you like; they are tried in order. An invocation carries a `!`, so a reader
always knows expansion is happening. Definitions are gathered from every file
before any is parsed, so a macro may be used above where it is written and
across module boundaries.

| Fragment | Matches |
|---|---|
| `$x: expr` | a balanced run of tokens, stopping at a top-level `,` or `;` |
| `$x: ty` | the same, for a type |
| `$x: ident` | exactly one name |
| `$x: literal` | one literal |
| `$x: block` | a `{ ... }` group |
| `$x: tt` | one token tree |

`$( ... )sep*` matches a repetition — `*` for none-or-more, `+` for
one-or-more. The separator goes **inside** the body when the expansion needs
one between statements: `$( push($x); )*`.

An expansion that is a single expression is spliced in parentheses so it
composes like the one value it is; several statements are spliced as they
stand. Which of the two is decided on the expanded tokens, since a repetition
hides its separator until then.

### Visibility

A macro is private to the file it is written in. `pub macro` puts it in scope
everywhere — every module of the package, and every package building against
it.

```rune
macro helper { ... }        // this file only
pub macro vec { ... }       // everywhere
```

Reaching another package works because a `.rul` carries its modules' source
alongside their object code. That source is read back before anything is
parsed, so the macros in it join the table like any other, and a package that
ships a macro ships it the way it ships a function.

The default matters more than it does for a function. Expansion is one pass
over the whole compilation, so without a private default every helper macro
anyone wrote would be in scope in every file at once. A macro is also **not
namespaced**: `vec!` is written `vec!`, never `vector::vec!`, because expansion
happens before imports mean anything — which is why keeping the default private
matters.

Two names collide when both are `pub`, or when both are in one file. Two
private macros of the same name in different files never meet.

### The macros the standard library provides

| Macro | Module | Does |
|---|---|---|
| `vec!(a, b, c)` | `std::collections::vector` | a `Vector` of the values given; `vec!()` is empty |
| `assert!(cond)` | `std::testing` | checks it, naming the check after the expression |
| `assert!(cond, name)` | `std::testing` | the same, with a name of your own |
| `assertEq!(got, want)` | `std::testing` | compares, naming the check `got == want` |
| `assertEq!(got, want, name)` | `std::testing` | with a name of your own |
| `assertNot!(cond)` | `std::testing` | the negative of `assert!` |
| `stringify!(...)` | the compiler | the argument tokens as the text they were written as |

```rune
let v = vec!(1, 2, 3)
assertEq!(v.length(), 3)      // ✓ v.length() == 3
assert!(twice(21) == 42)      // ✓ twice(21) == 42
```

Each assertion takes its name from `stringify!`, so a failing line says what
was being checked without the check having been named twice.

## 17. Current limitations

* Array lengths must be *constant* expressions. Use a slice, or slice an
  oversized buffer with `values[0..count]`, when the length is only known at
  run time.
* Operators cannot be overloaded for builtin types: `bind operator::add to
  i64` is rejected, because the builtin meaning would always win.
* The package registry checks every archive against the checksum its index
  recorded, but the index itself is not signed, and nothing can be yanked.
  A registry is whoever runs it; read what you depend on.
* `import std::{io, collections}` is refused, because `collections` is a
  nested prefix rather than a module. Import what is under it
  (`std::collections::vector`). Making every prefix a real namespace module
  would fix this generally.
* There is no closure-to-`dyn` coercion and no async.
* Generics are monomorphised, and there are no higher-kinded parameters: a
  parameter stands for a type, never for a type constructor.
* The iterator adaptors are `map`, `filter`, `zip`, `take_while`,
  `skip_while`, `take`, `skip`, `enumerate`, `chain`, `step_by`, `inspect`,
  `flatten` and `flatMap`, ended by `collect`, `count`, `find`, `any`, `all`,
  `fold`, `reduce`, `forEach`, `last`, `nth`, `position` or `best`. There is
  no `sum`, `min` or `max`: each waits on a way to name a type's zero and
  its ordering as a bound.
* A generic type may not be parameterised by a deeper version of itself:
  `struct Nest<T> { deeper: Nest<Nest<T>>? }` has no end, and is reported
  rather than run out of stack. A recursive structure holds *itself* —
  `Nest<T>?` — which is fine.
* A `dyn Iterator` cannot be iterated: a mark object does not carry the
  binding's associated types, so `Item` is unknown at the loop.
* `value is T` only takes a name for `T`, because that is where a pattern
  would have gone. An `Any` holding a type with no bare-name spelling —
  `[3:f64]`, `(i64, String)`, `dyn Shape` — is asked with `.holds::<T>()` and
  `.get::<T>()` instead.
* A reference cycle running through an `Any` is not detected. The strong-edge
  check reads types, and an `Any` names none until the program runs.
* At `--safety full` a class that can reach itself through strong references
  is **refused**, so a reference cycle cannot be built: counting frees an
  object when the last reference goes, and a ring never reaches zero. The rule
  reads *types*, so it also refuses shapes that would never have looped — a
  forward-linked `class Node { next: Node? }`, or a doubly-linked list whose
  `prev` is already `weak`. `--safety minimal` allows those and reports what
  is left at exit; holding nodes in a `Vector` and linking by index avoids the
  question entirely. One thing the rule cannot see: a closure's captures are
  not part of its type, so a class holding a closure that captures it is a
  ring the check misses.
* Closures copy their captures into a heap environment when the closure is
  made. `move ||(...)` states that explicitly; to share one value instead,
  capture a class (`mem::Handle<T>` is one for a single value). Assigning to a
  captured name only changes the copy, and is warned about.
* `std::text` normalises to NFC/NFD and compares with a root collation — base
  letters, then accents, then case. It is not the Unicode Collation Algorithm
  and has no per-locale orderings, and its case folding is simple rather than
  full (`ß` stays one character). Its everyday half — `split`, `splitLines`,
  `join`, `trim`, `replace`, `startsWith`, `endsWith`, `contains`, `find`,
  `withoutPrefix`, `withoutSuffix`, `padStart`, `padEnd` — works on bytes
  rather than code points, which is exact for the ASCII delimiters a
  separator almost always is. There is no `upper` or `lower`: full case
  mapping is locale- and context-sensitive and not one-to-one, so it needs a
  table of its own.
* Errors propagate with `Result` and `?`. A panic aborts and cannot be caught:
  there is no unwinder, and unwinding past a scope would have to release
  everything that scope owns.
* `-g` emits full DWARF — subprograms, line tables, types, parameters and
  locals — and a demangled traceback when a program aborts. Lexical sub-scopes
  are flattened into the enclosing function, so a shadowed name shows the
  outer one.
* `std::io` covers directories as well as files: `readDirectory` lists one
  level, `walkDirectory` walks a tree and returns files only, `isDirectory`
  asks, and `joinPath` puts exactly one separator between two pieces. Listing
  order is the filesystem's, which is neither alphabetical nor the same on two
  machines. There is no metadata (size, times), no rename or copy, and no
  `std::path`.
* `std::net` is TCP over the platform's sockets. There is no UDP, no
  non-blocking or polled I/O beyond `setReadTimeout`, and no TLS.
