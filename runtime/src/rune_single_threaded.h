/* WebAssembly built without threads (`wasm32-wasip1`): starting a thread
 * cannot succeed there, so the three calls that would try are answered here,
 * the way a machine that had run out of threads would answer them.
 *
 * wasi-libc's own answers live in a separate library,
 * `libwasi-emulated-pthread`, which only some WASI SDKs ship — not older
 * ones, and not every packaging of wasi-libc (Homebrew's, for one). Answering
 * here means nothing beyond libc is linked, whichever SDK built the program.
 * Include after <pthread.h>. */
#ifndef RUNE_SINGLE_THREADED_H
#define RUNE_SINGLE_THREADED_H
#if defined(RUNE_SINGLE_THREADED)
#include <errno.h>
#define pthread_create(thread, attr, start, arg)                               \
  ((void)(thread), (void)(attr), (void)(start), (void)(arg), EAGAIN)
#define pthread_join(thread, result) ((void)(thread), (void)(result), ESRCH)
#define pthread_detach(thread) ((void)(thread), ESRCH)
#endif
#endif
