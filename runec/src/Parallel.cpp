#include "rune/Parallel.h"

#include <cstdlib>

namespace rune {

unsigned parallelism() {
  static const unsigned count = [] {
    if (const char *env = getenv("RUNE_JOBS")) {
      int n = atoi(env);
      if (n > 0)
        return static_cast<unsigned>(n);
    }
    unsigned hw = std::thread::hardware_concurrency();
    return hw ? hw : 1u;
  }();
  return count;
}

} // namespace rune
