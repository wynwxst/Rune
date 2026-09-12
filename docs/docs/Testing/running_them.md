# Running them

| Command | Does |
| --- | --- |
| `rune test` | builds and runs every file under `tests/` |
| `rune test --release` | the same, optimised |
| `rune test -v` | shows each compiler command |

Each file is compiled as its own program and linked against the package, so a test can import the library it is testing by name. The summary line counts *files*; the per-check tally is printed by each file as it runs.

> [!NOTE]
> **Aborts count as failures**
>
> A test that aborts — a failed bounds check, an explicit `process::panic` — is a failure like any other, because the exit status is non-zero. You do not have to catch anything.

> [!NOTE]
> **It is there from the start**
>
> `rune new` scaffolds `tests/basics.rune` already written this way, so a fresh package has a passing test before you have written any code.
