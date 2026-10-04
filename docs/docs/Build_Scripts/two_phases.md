# Two phases

`rune` compiles `build.rune` for the machine doing the building, whatever the package is built for, and runs it twice per build, in the package's directory:

| Phase | Runs | Can |
| --- | --- | --- |
| **prepare** | before the package is compiled | set `#Config` flags; add link arguments, libraries and library directories |
| **finish** | after each executable is linked, once per executable | post-process the file — strip it, sign it, lay it out as an image — and say what `rune run` should start in its place |

It is an ordinary program with a `main`. `std::build` tells it which phase this is and everything else it may want to know, and gives it a function for each answer.

**Both phases**

```text
// build.rune
import std::build

fn main() -> i64 {
    if build::preparing() {
        if build::release() { build::cfg("fast_paths") }
        build::linkLibrary("z")
    }
    if build::finishing() {
        let image = build::artifact() + ".img"
        let objcopy = build::tool(["llvm-objcopy", "objcopy"])
        build::run(objcopy, ["-O", "binary", build::artifact(), image.$clone()])
        build::runWith(image)       // `rune run` boots this
    }
    0
}
```
