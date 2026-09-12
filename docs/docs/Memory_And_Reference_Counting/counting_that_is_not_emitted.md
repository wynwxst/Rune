# Counting that is not emitted

At `--safety full` the ownership pass works out which locals never leave the scope that declared them, and a class built for one of those needs no counting: nothing else can reach it, so the allocation's own reference *is* the binding's and the scope hands it back on the way out. Nothing has to be written to get this and nothing observable changes — `process::liveObjectCount()` agrees either way. What changes is that the retain, the temporary slot and the paired release are simply not emitted.

> [!NOTE]
> **Only where it is provable**
>
> Anything the pass cannot follow escapes: a capture, a raw-pointer cast, a store into a field, a call taking it by value. So this applies where the whole story is visible in one body, and nowhere else.
