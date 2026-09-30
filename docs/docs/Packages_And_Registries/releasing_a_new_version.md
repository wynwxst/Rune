# Releasing a new version

Bump `version` in the manifest, make the change, and add the package again: the index gains a release and keeps the old one, so a project pinned to it goes on building. `examples/package/` keeps one directory per released version — `packages/stats/2.0.0/`, `packages/stats/2.1.0/` — and rebuilds the whole registry from them with `python3 ecosystem.py build`.

```sh
$ sed -i 's/^version = "2.0.0"/version = "2.1.0"/' stats/Rune.toml
$ rune registry --addPackage stats --dir registry
● Added stats v2.1.0 (9.0 KB, sha256 d00426eaff5b…)
$ cd ../dashboard && rune update
○ Fetching stats v2.1.0
● Installed stats v2.1.0
● stats: v2.0.0 -> v2.1.0
● 1 installed package version is not used by any project; `rune remove` with no arguments uninstalls it
```

> [!WARNING]
> **Trust**
>
> Every install checks the archive against the checksum the index recorded, which catches a corrupted or tampered file. It does not vouch for the package: a registry added with `rune registry add` is whoever runs it, and nothing reviews what it serves. Read what you depend on, and keep `Rune.lock` so what you read is what you build.
