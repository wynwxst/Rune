# Workspaces

Packages that are worked on together — a program and the libraries it is split into — can share one directory as a workspace. Its `Rune.toml` has a `[workspace]` table naming the packages under it, its members; each member is an ordinary package with a manifest of its own, and members use one another by `path`. `rune ws` makes and edits the workspace and runs a command over every member, a member always after the members it uses.

```sh
$ rune ws new studio && cd studio
$ rune ws add geometry --lib     # makes the package when there is none
$ rune ws add app
$ cat Rune.toml
[workspace]
members = [
    "app",
    "geometry",
]
$ rune ws list
  geometry  geometry  [lib]
  app  app  [bin]  uses geometry
$ rune build                     # at the root: every member, geometry first
$ rune ws run app
```

| Command | Does |
| --- | --- |
| `rune ws new <dir>` / `rune ws init` | make a workspace; `init` takes in the packages already below |
| `rune ws add <dir> [--lib]` | add a member, making the package first if there is none |
| `rune ws remove <dir>` | stop treating it as a member; its files stay |
| `rune ws list` | the members, in the order they build |
| `rune ws build` / `check` / `test` / `clean` / `doc` | the command in every member; the same as the bare command at the root of a workspace that is not a package itself |
| `rune ws run <member> [args]` | run one member's program |

> [!NOTE]
> **From a member**
>
> Inside a member, `rune build` builds that member and what it uses. `rune ws` works from anywhere inside the workspace: it looks upward for the `[workspace]` table, as `rune` looks for the package around it.
