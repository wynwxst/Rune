// The foreign half of `A4_c_abi`: C functions that take and return structs
// by value, in each shape the x86-64 and AArch64 conventions treat
// differently, and one that calls back into Rune with them.
#include <cstdint>
#include <cstdio>

struct Wide { double x, y, w, h; };          // 32 bytes: in memory on x86-64
struct Three { int32_t a, b, c; };           // two eightbytes, packed
struct Mixed { float f; int32_t i; double d; };

extern "C" Wide rune_cabi_grow(Wide r, double by) {
    return Wide{r.x - by, r.y - by, r.w + 2 * by, r.h + 2 * by};
}

extern "C" Three rune_cabi_rotate(Three t) { return Three{t.b, t.c, t.a}; }

extern "C" double rune_cabi_mixed(Mixed m) { return m.f + m.i + m.d; }

extern "C" double rune_cabi_callback(double (*area)(Wide), Three (*spin)(Three)) {
    Three t = spin(Three{1, 2, 3});
    std::printf("callback spin %d %d %d\n", t.a, t.b, t.c);
    std::fflush(stdout);
    return area(Wide{0, 0, 3, 5});
}
