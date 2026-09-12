# What a condition can ask

| Key | Is |
| --- | --- |
| `os` | `macos`, `windows`, `linux`, `ios`, `android`, `freebsd`, `openbsd`, `netbsd`, `solaris`, `wasi` |
| `arch` | `aarch64`, `x86_64`, `x86`, `arm`, `riscv32`, `riscv64`, `wasm32`, `wasm64`, `powerpc64` |
| `family` | `unix`, `windows` or `wasm` |
| `pointer_width` | `"32"` or `"64"` |
| `endian` | `little` or `big` |
| `target` | the full triple being built for |
| `safety` | `none`, `minimal` or `full` |
| `memory` | `arc` or `zombie` |
| `opt_level` | `"0"` through `"3"` |

Those are compared against a string. Everything else is a name that is either set or not, written on its own:

| Name | Set when |
| --- | --- |
| `debug` | the build carries debug information (`-g`) |
| any `--cfg <name>` | the command line said so |
| any `[build] cfg` entry | the manifest said so |
| a dependency's name | that dependency is in `[dependencies]` |

A condition joins those with `&&`, `||`, `!` and parentheses. There is nothing else in the language: no arithmetic, no calls, no variables. It has to be answerable before the type checker has run.

**Conditions compose**

```rune
@Config(arch == "aarch64" && os == "macos")
fn tuned() -> i64 { 1 }

@Config(!(arch == "aarch64" && os == "macos"))
fn tuned() -> i64 { 0 }

@Config(debug)
fn checking() -> bool { true }

@Config(!debug)
fn checking() -> bool { false }
```
