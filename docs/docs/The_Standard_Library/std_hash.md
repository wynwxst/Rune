# std::hash

Hashes with a name, for when the structural `mem::hash` is not what is wanted: a hash that is written to a file, sent over a wire, or compared with one someone else computed has to be a *particular* hash. FNV-1a and CRC-32 are for tables and checksums; SHA-256 is the one to use where a collision would matter. Each is a function over a `String` or a `[u8]`, and a struct that can be fed a piece at a time.

| Name | Signature | Does |
| --- | --- | --- |
| `fnv1a64` / `fnv1a64Bytes` | `(String) -> u64` / `([u8]) -> u64` | FNV-1a, 64 bits |
| `fnv1a32` | `(String) -> u32` | FNV-1a, 32 bits |
| `crc32` / `crc32Bytes` | `(String) -> u32` / `([u8]) -> u32` | CRC-32 as zip and PNG use it |
| `sha256` | `(String) -> String` | the digest as 64 hex characters |
| `sha256Bytes` | `([u8]) -> [32:u8]` | the digest itself |
| `Fnv64`, `Fnv32`, `Crc32`, `Sha256` | `struct` | incremental: `feed(u8)`, `feedBytes([u8])`, `feedText(String)`, then `finish()` |
| `hex` / `hex64` / `hex32` | `([u8]) -> String` and friends | lowercase hexadecimal |

**Named hashes**

```rune
import std::io
import std::hash

fn main() -> i64 {
    io::println(hash::hex64(hash::fnv1a64("hello")))
    io::println(hash::hex32(hash::crc32("hello")))
    io::println(hash::sha256("hello"))

    var h = hash::Sha256 {}              // the same digest, fed in pieces
    h.feedText("hel")
    h.feedText("lo")
    io::println(h.finishHex() == hash::sha256("hello"))
    0
}
```
