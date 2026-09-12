//===- Parallel.h - Running independent work on several cores --*- C++ -*-===//
//
// The compiler has a few passes whose items genuinely do not depend on one
// another: lexing a file, parsing a file. This is the whole of what it takes
// to spread those over the cores that are there.
//
// Nothing here schedules or steals. Each worker walks the index space in
// strides, so the work divides itself without a queue, and a pass whose items
// take wildly different times still finishes in roughly the time of the
// slowest core's share rather than the slowest item.
//
//===----------------------------------------------------------------------===//
#ifndef RUNE_PARALLEL_H
#define RUNE_PARALLEL_H

#include <algorithm>
#include <cstddef>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace rune {

/// How many threads the compiler may use. `RUNE_JOBS` overrides the count the
/// hardware reports, which is what a build system driving several compiles at
/// once needs in order not to oversubscribe the machine.
unsigned parallelism();

/// Runs `body(i)` for every `i` in `[0, count)`, on `parallelism()` threads.
///
/// Calls the body directly when there is only one thing to do or only one
/// thread to do it on, so a small compile pays nothing for the machinery.
/// An exception escaping the body is rethrown here once every worker has
/// stopped, so no thread is left running over freed state.
template <typename Body> void parallelFor(size_t count, Body body) {
  const unsigned threads =
      static_cast<unsigned>(std::min<size_t>(parallelism(), count));
  if (threads <= 1) {
    for (size_t i = 0; i < count; ++i)
      body(i);
    return;
  }

  std::mutex errorMutex;
  std::exception_ptr failure;
  std::vector<std::thread> workers;
  workers.reserve(threads - 1);

  auto stride = [&](unsigned first) {
    for (size_t i = first; i < count; i += threads) {
      try {
        body(i);
      } catch (...) {
        std::lock_guard<std::mutex> lock(errorMutex);
        if (!failure)
          failure = std::current_exception();
        return;
      }
    }
  };

  for (unsigned t = 1; t < threads; ++t)
    workers.emplace_back(stride, t);
  stride(0);
  for (std::thread &w : workers)
    w.join();

  if (failure)
    std::rethrow_exception(failure);
}

} // namespace rune

#endif
