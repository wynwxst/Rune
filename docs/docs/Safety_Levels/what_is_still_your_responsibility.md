# What is still your responsibility

Memory safety here means no out-of-bounds access, no use of a released object, and no dereference of `nil` — inside safe code. It does not mean your program cannot deadlock, cannot exhaust memory, or cannot leak a reference cycle. Cycles are diagnosed at exit rather than prevented, and `weak` is the tool for them.

The borrow check is function-local. A borrow handed to a call is the callee's business for the length of the call, and a raw pointer is not followed at all — which is what `unsafe` means.
