# What it buys at run time

The same pass answers a question nothing reports: which locals never leave the scope that declared them. A class built for one of those needs no reference counting at all — nothing else can hold it, so the allocation's own count *is* the binding's, and the scope hands it back on the way out. The retain, the temporary and the paired release all go.

```sh
$ runec --emit-llvm demo.rune -o demo.ll

  ; without the analysis
  %Point = call ptr @rune_alloc(i64 24, ptr @typeinfo)
  call void @Point_init(ptr %Point, i64 3)
  store ptr %Point, ptr %temp
  %0 = call ptr @rune_retain(ptr %Point)
  %1 = load ptr, ptr %p
  call void @rune_release(ptr %1)
  store ptr %Point, ptr %p
  %2 = load ptr, ptr %temp
  call void @rune_release(ptr %2)

  ; with it
  %Point = call ptr @rune_alloc(i64 24, ptr @typeinfo)
  call void @Point_init(ptr %Point, i64 3)
  store ptr %Point, ptr %p
```

Anything the pass cannot follow is treated as escaping: a value captured by a closure, cast to a raw pointer, stored in a field, or passed to a call by value. So the elision only ever applies where the whole story is visible in one body.
