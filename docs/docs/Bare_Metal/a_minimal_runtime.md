# A minimal runtime

A program that has to be small can ask for only what it cannot run without: `freestanding_type = "minimal"` in the package's `[config]`, or `--cfg freestanding_type=minimal` to `runec`. The panics, the heap, memory and raw memory, objects, and a `String` made, joined, compared and printed, with integers and booleans as text, all stay. Left out are floats as text — the big-integer arithmetic that prints a double exactly — text as numbers, `$substring`, `$repeat`, `$find` and walking characters, `$clone()` of a class object, `Any` and class tests, and hashing. The parts are marked `#Config(!(freestanding_type == "minimal"))` in `runetime/`, so the line is drawn where it can be read.

```sh
[config]
freestanding_type = "minimal"     # or "full", the default
```

A program that needs a part the minimal runtime leaves out is told so when it is compiled, against its own function, not by the linker:

```sh
fn report(x: f64) -> String { "x = " + x.$str() }
   ^^^^^^ ERROR: 'report' needs more of the runtime than
          `freestanding_type = "minimal"` keeps [E0542]
     ─  note: it uses floats as text, which the minimal freestanding runtime leaves out
     ─  note: `freestanding_type = "full"` (the default) has it
```

Function by function, the line is in the standard library's reference: what the minimal runtime cannot run carries the badge **bare metal · full runtime**, and `runec --tiers` lists it as `full`, with the runtime function it needs.

> [!NOTE]
> **What it saves**
>
> A small program that builds strings and prints numbers came to an object of 7.8 KB with the minimal runtime against 27 KB with the full one. Any other value of `freestanding_type` is E0545.
