# The two marks

**What `std::iter` declares**

```text
mark Iterator {
    type Item
    fn next(&var self) -> Self::Item?          // the one thing to supply

    // Adaptors: each wraps this iterator in another one.
    fn map<B>(self, f: @function(Self::Item) -> B) -> Map<Self, B>
    fn filter(self, keep: @function(Self::Item) -> bool) -> Filter<Self>
    fn zip<J: Iterator>(self, other: J) -> Zip<Self, J>
    fn take_while(self, keep: @function(Self::Item) -> bool) -> TakeWhile<Self>
    fn skip_while(self, drop: @function(Self::Item) -> bool) -> SkipWhile<Self>
    fn take(self, count: i64) -> Take<Self>
    fn skip(self, count: i64) -> Skip<Self>
    fn enumerate(self) -> Enumerate<Self>
    fn chain<J: Iterator>(self, other: J) -> Chain<Self, J>
    fn as_iter(self) -> Self                   // already one; here for symmetry

    // Ending a chain: these are what run it.
    fn collect(self) -> vector::Vector<Self::Item>
    fn count(self) -> i64
    fn find(self, keep: @function(Self::Item) -> bool) -> Self::Item?
    fn any(self, keep: @function(Self::Item) -> bool) -> bool
    fn all(self, keep: @function(Self::Item) -> bool) -> bool
}

mark Sequence {
    type Iter: Iterator
    fn iterate(&self) -> Self::Iter

    // Every adaptor again, one step earlier: each asks for a cursor first.
    // `as_iter` is `iterate` under the name a chain reads better with.
}
```

An `Iterator` is a cursor: `next` hands back a value each turn and `nil` when it runs out. A `Sequence` is something a fresh cursor can be had from. `Iterator` is asked first, so a type that is both stays its own iterator.
