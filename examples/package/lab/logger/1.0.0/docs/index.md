# logger (lab)

The lab's build of the logger: everything 0.9.0 has, a `Trace` level under
`Debug`, `louder()` to match `quieter()`, and `summary()`.

```rune
import logger

var log = logger::standard("app")
log.info("starting")               // [app info] starting
log.trace("very quiet")            // dropped: below Info
log.louder()
log.louder()
log.trace("now printed")           // [app trace] now printed
io::println(log.summary())         // 2 printed, 1 dropped
```

It is published in the `lab` registry, not the main one, so a project that
wants it says so: `rune add lab::logger`, which the manifest records as
`logger = { version = "1.0", registry = "lab" }`.
