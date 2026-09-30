// The foreign half of `A3_typeof`: a function Rune declares variadic and
// calls through the exact signature `typeof` built for it, so it can check
// every argument arrived where the ABI says it should.
#include <cstdint>
#include <cstdio>

struct RuneTestRect { double x, y, w, h; };

extern "C" void *rune_test_probe(void *a, void *b, RuneTestRect r,
                                 uint64_t style, int n, signed char flag) {
    (void)a; (void)b;
    std::printf("probe %.1f %.1f %.1f %.1f style %llu n %d flag %d\n",
                r.x, r.y, r.w, r.h, (unsigned long long)style, n, (int)flag);
    std::fflush(stdout);
    return reinterpret_cast<void *>(0x2a);
}
