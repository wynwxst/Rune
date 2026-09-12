# logger

Lines with a level. A `Logger` prints what is at or above its threshold and
counts what it dropped, so a test can ask whether anything was too quiet.

```rune
import logger

var log = logger::standard("app")
log.info("starting")               // [app info] starting
log.debug("details")               // dropped: below Info
```
