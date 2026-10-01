# Raw targets

A table with a `triple` of its own — no `base`, not named after a foreign target — is **raw**: the configuration is entirely the table's. The build adds no flag of its own anywhere:

| Step | Command |
| --- | --- |
| The runtime | `<cc> -c <c-flags> -I<runtime headers> -o ... <source>`, built in the project, in `target/<name>/runtime/` |
| `c-sources` | `<cc> -c <c-flags> <package c-flags> -o ... <source>` — no `-fPIC`, `-O`, `-g` or `-std` of the build's choosing |
| The link | `<linker> <objects> -o <output> <runtime> <link-args> -L... -l...` — no `-lm`, `-rdynamic`, `-nostdlib` |

The runtime is a raw target's own, even when one for the same triple is already cached in `~/.rune/runtime/`: that one was compiled with flags this target did not ask for. It is built again whenever its command lines, its sources or the toolchain change — a new `c-flags` is a new runtime.

**Rune.toml**

```toml
[target.x]
triple = "x86_64-unknown-linux-gnu"
cc = "cc"
c-flags = ["-O1", "-g"]
link-args = ["-lm"]
runner = "env"
```

```sh
$ rune build --target x -v
○ Preparing runtime for x86_64-unknown-linux-gnu (target/x/runtime)
  'cc' -c '-O1' '-g' -I'.../runtime/include' -o '.../target/x/runtime/rune_runtime.o' ...
○ link: 'cc' '.../target/x/debug/app.o' '-o' '.../target/x/debug/app' '.../target/x/runtime/libruneruntime.a' '-lm'
```

> [!NOTE]
> **Either way**
>
> `default-flags = true` in the table brings the build's flags back, and the shared runtime with them; `default-flags = false` makes a table that starts from a foreign target raw too. `runtime-dir` still names a prebuilt runtime instead of either.
