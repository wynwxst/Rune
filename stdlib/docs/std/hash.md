# std::hash

Hashes with a name, for when the structural `mem::hash` is not what is
wanted. A hash that is written to a file, sent over a wire, or compared with
one someone else computed has to be a *particular* hash — and these are.
FNV-1a and CRC-32 are for tables and checksums; SHA-256 is the one to use
where a collision would matter.

## In one call

```rune
import std::io
import std::hash

fn main() -> i64 {
    io::println(hash::hex64(hash::fnv1a64("hello")))     // a430d84680aabd0b
    io::println(hash::hex32(hash::fnv1a32("hello")))
    io::println(hash::crc32("hello"))                    // 907060870
    io::println(hash::sha256("hello"))
    let bytes: [3:u8] = [104, 105, 33]
    io::println(hash::hex32(hash::crc32Bytes(bytes)))
    io::println(hash::hex(hash::sha256Bytes(bytes)).$length())
    0
}
```

## A piece at a time

Each algorithm is also a struct that is fed as the input arrives. `finish`
leaves the hasher as it was, so more can be fed afterwards.

```rune
import std::io
import std::hash

fn main() -> i64 {
    var h = hash::Sha256 {}
    h.feedText("hel")
    h.feedText("lo")
    io::println(h.finishHex() == hash::sha256("hello"))

    var c = hash::Crc32 {}
    for b in [104u8, 101u8, 108u8, 108u8, 111u8] { c.feed(b) }
    io::println(c.finish() == hash::crc32("hello"))

    var f = hash::Fnv64 {}
    f.feedText("key")
    io::println(hash::hex64(f.finish()))
    0
}
```
