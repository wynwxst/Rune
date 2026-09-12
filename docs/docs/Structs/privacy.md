# Privacy

A field with no `pub` is visible inside its own module and nowhere else. The same goes for methods.

```rune
pub struct Token {
    pub text: String        // visible to importers
    offset: i64             // module-private

    pub fn length(&self) -> i64 { self.text.$length() }
    fn internalOffset(&self) -> i64 { self.offset }
}
```
