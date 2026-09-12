# Linking

Declaring a foreign function does not find it. Point the linker at the library with `-l` on the command line, or `link = [...]` in the manifest, where dependents inherit it automatically.

```sh
$ runec -o plot src/main.rune -L /usr/local/lib -l m -l png
```

**The same, in Rune.toml**

```toml
[package]
name = "statistics"
version = "0.1.0"

[build]
# Dependents of this package inherit `-l m` without repeating it.
link = ["m"]
link-paths = ["/usr/local/lib"]
```
