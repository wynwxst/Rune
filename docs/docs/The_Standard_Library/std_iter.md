# std::iter

The two marks `for` dispatches through, and everything they carry. Both marks are in scope everywhere, and every adaptor a `Sequence` has is the `Iterator` one with an `iterate` in front of it; see [Iterators](#iterators).

> [!NOTE]
> **Returning a chain**
>
> A function that returns a chain writes `-> some Iterator` rather than the nested wrapper type. The adaptors themselves still return `Map<Self, B>` and friends — `some` is how a caller is spared from writing those out. See [Opaque results: `some Mark`](#marks).

| Member | Signature | Does |
| --- | --- | --- |
| `Iterator` | `mark { type Item; next; … }` | a cursor over values |
| `next` | `(&var self) -> Self::Item?` | the next value, `nil` at the end — the one thing to supply |
| `map` | `<B>(self, @function(Self::Item) -> B) -> Map<Self, B>` | every value, with `f` applied |
| `filter` | `(self, @function(Self::Item) -> bool) -> Filter<Self>` | only the values `keep` says yes to |
| `zip` | `<J: Iterator>(self, J) -> Zip<Self, J>` | pairs, ending with the shorter |
| `take_while` | `(self, @function(Self::Item) -> bool) -> TakeWhile<Self>` | up to the first `false`; the source is not asked again |
| `skip_while` | `(self, @function(Self::Item) -> bool) -> SkipWhile<Self>` | everything from the first `false` onwards |
| `take` / `skip` | `(self, count: i64) -> Take<Self>` / `Skip<Self>` | at most `count`, or all but the first `count` |
| `enumerate` | `(self) -> Enumerate<Self>` | each value paired with its position |
| `chain` | `<J: Iterator>(self, J) -> Chain<Self, J>` | this one's values, then the other's |
| `flatten` | `(self) -> Flatten<Self> where Self::Item: Iterator` | every value of every inner iterator, laid out flat |
| `flatMap` | `<J: Iterator>(self, @function(Self::Item) -> J) -> Flatten<Map<Self, J>>` | `map` and `flatten` in one |
| `as_iter` | `(self) -> Self` | itself; a `Sequence`'s hands out a cursor instead |
| `collect` | `(self) -> vector::Vector<Self::Item>` | runs the chain out into a vector |
| `count` | `(self) -> i64` | how many are left |
| `find` | `(self, @function(Self::Item) -> bool) -> Self::Item?` | the first match, stopping there |
| `any` / `all` | `(self, @function(Self::Item) -> bool) -> bool` | stopping at the first yes, or the first no |
| `step_by` | `(self, stride: i64) -> StepBy<Self>` | every `stride`-th value |
| `inspect` | `(self, @function(Self::Item) -> ()) -> Inspect<Self>` | every value unchanged, with a look at each on the way past |
| `forEach` | `(self, @function(Self::Item) -> ())` | every value, one at a time, with nothing kept |
| `fold` | `<A>(self, initial: A, @function(A, Self::Item) -> A) -> A` | the general form of every terminator above |
| `reduce` | `(self, @function(Self::Item, Self::Item) -> Self::Item) -> Self::Item?` | `fold` with the first value as the seed |
| `last` | `(self) -> Self::Item?` | the last one; walks to the end |
| `nth` | `(self, index: i64) -> Self::Item?` | the one at `index`, counting from zero |
| `position` | `(self, @function(Self::Item) -> bool) -> i64?` | where the first match is, rather than what it is |
| `best` | `(self, @function(Self::Item, Self::Item) -> bool) -> Self::Item?` | the value the comparison prefers over every other |
| `Sequence` | `mark { type Iter: Iterator; iterate; … }` | something a cursor can be had from; carries all of the above |
| `iterate` | `(&self) -> Self::Iter` | a fresh cursor |
| `counting` / `countingBy` | `(i64[, i64]) -> Counter` | integers, endlessly |
| `vector::collect` | `<I: Iterator>(I) -> Vector<I::Item>` | `collect` written the other way round |
| `vector::VectorIter<T>` | `Iterator` | what `Vector<T>` hands out |
| `text::chars` | `(String) -> Chars` | the characters of a String, in one pass |

**Ending a chain**

```rune
import std::io
import std::iter
import std::collections::vector

fn main() -> i64 {
    let scores = vec!(3, 1, 4, 1, 5)

    // `fold` is the general form: a running answer and a step. `count`,
    // `any` and `all` are all folds with the step already written.
    io::println(scores.as_iter().fold(0, ||(sum: i64, n: i64) -> i64 {
        sum + n
    }).$str())

    // `reduce` seeds itself from the first value, so an empty chain has an
    // answer — `nil` — rather than needing one invented.
    io::println((scores.values().reduce(||(a: i64, b: i64) -> i64 {
        if a > b { a } else { b }
    }) ?? -1).$str())

    // `find` hands back the value; `position` hands back where it was.
    io::println((scores.as_iter().position(||(n: i64) -> bool { n == 4 }) ?? -1).$str())
    io::println((scores.as_iter().nth(2) ?? -1).$str())

    // `best` takes the comparison rather than requiring a bound: it returns
    // true when its first argument should win.
    io::println((scores.as_iter().best(||(a: i64, b: i64) -> bool { a < b }) ?? -1).$str())

    // `inspect` is the chain's own print statement. The chain is lazy, so
    // only the two values `take` asks for ever go past it.
    io::println(scores.as_iter().inspect(||(n: i64) -> () {
        io::println("saw " + n.$str())
    }).take(2).count().$str())

    for n in iter::counting(0).step_by(5).take(4) { io::println(n.$str()) }
    0
}
```
