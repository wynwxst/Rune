# Where this works

The threading layer has two backings: pthreads, and the Win32 calls on Windows. The pthreads one is what every example on this page runs on, and what the test suite exercises.

| Platform | State |
| --- | --- |
| macOS, Linux, and anything with pthreads | works, and is what the tests run against |
| Windows | **not yet**: a single `spawn`, several concurrent ones, and `Mutex` all work, but destroying a handle and spawning again faults. Everything else cross-compiles and runs; only threads are affected |

> [!WARNING]
> **Windows**
>
> Said plainly because a reference that hides its edges wastes your time: if you are targeting Windows, do not use `std::thread` yet. The rest of the language is unaffected.
