# What a version promises

Versions are `major.minor.patch`, and a requirement written without an operator — `"0.3"`, `"1.2.3"` — accepts anything with the same leading non-zero part: `"1.2"` takes 1.9.0 but not 2.0.0, and `"0.3"` takes 0.3.7 but not 0.4.0. That is the promise a version makes: a change to the leading part may break a caller, and anything smaller must not. The rule of thumb, then:

| Bump | When |
| --- | --- |
| patch | a fix; nothing public changed |
| minor | something public was added; nothing was taken away or changed |
| major (or the minor, below 1.0) | something public changed or went away |

*`stats` 2.0.0 → 2.1.0 added `percentile`; `units` 1.0.0 → 1.1.0 added `Temperature`.*

What a dependency may ask for, then, in the manifest or after `@` on the command line (`rune add stats@~2.0`):

| Requirement | Accepts |
| --- | --- |
| `"2.1.0"`, `"^2.1.0"` | >=2.1.0 and <3.0.0 — the same leading non-zero part |
| `"2.1"`, `"2"` | the same, with the missing parts read as 0: >=2.1.0 <3.0.0; >=2.0.0 <3.0.0 |
| `"0.3"` | >=0.3.0 and <0.4.0 — below 1.0, the minor is the leading part |
| `"~2.1"` | >=2.1.0 and <2.2.0 |
| `"=2.1.0"` | exactly that |
| `">=2.0, <2.2"` | every comparison listed — `>=`, `>`, `<=`, `<` and `=`, separated by commas |
| `"*"` | anything |

*Cargo's spellings, since they are the ones people know. The same table is under *The toolchain → Dependencies*.*

A requirement is checked against what exists whenever it is acted on: `rune add stats@=2.2.0` refuses when no such version is in any registry, and a manifest edited by hand to say so is caught by the next `rune build` — the lock's pin is not trusted past what the manifest now asks for, so the build resolves again and fails with the versions there are. `rune deps` marks such a pin *stale* until then.
