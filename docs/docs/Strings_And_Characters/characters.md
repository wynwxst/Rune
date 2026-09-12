# Characters

**Comparing, converting, and walking**

```rune
import std::io

fn isVowel(c: Character) -> bool {
    c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u'
}

fn main() -> i64 {
    io::println(isVowel('e'))
    io::println(isVowel('z'))
    io::println('a' < 'b')
    io::println('A' as i64)
    io::println(97 as Character)

    // Walking a string by character.
    let text = "hello"
    var vowels = 0
    var i = 0
    while i < text.$length() {
        if isVowel(text.$at(i)) { vowels += 1 }
        i += 1
    }
    io::println(vowels)
    0
}
```
