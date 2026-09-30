# Making one: `std::cxx`

A C++ object lives where C++ can destroy it, so its storage comes from `operator new` and goes back to `operator delete` — never from Rune's allocator. `cxx::alloc<T>()` is the first half and `cxx::free` the second, with the constructor and destructor written out between them, exactly as placement new and an explicit destructor call are in C++.

**The two halves of an object's life**

```text
import std::cxx

@safe("the storage is ours from `alloc` until `free`, and nothing else holds it")
fn counting() -> i64 {
    let c = cxx::alloc<Counter>()   // @size(8) bytes from `operator new`
    c.init(10)                      // Counter::Counter(10), on that storage
    c.setStep(5)
    let first = c.next()
    c.deinit()                      // ~Counter()
    cxx::free(c)                    // back to `operator delete`
    first as i64
}
```

> [!NOTE]
> **Or let C++ do it**
>
> A library that hands objects out and takes them back — `Counter::make` and `Counter::destroy` above — is simpler still: call those and let it do both halves. `cxx::alloc` is for the classes that have no such pair.

| `std::cxx` | Is |
| --- | --- |
| `alloc<T>() -> *var T` | `operator new(sizeof(T))`, uninitialised |
| `free<T>(p: *var T)` | `operator delete(p)` |
| `null<T>() -> *var T` | `nullptr` |
| `isNull<T>(p) -> bool` | `p == nullptr` |
