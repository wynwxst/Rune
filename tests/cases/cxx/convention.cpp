// The foreign half of `B6_convention_c`: C reading Rune's `@Convention("C")`
// types by value and in memory, and calling `@export`ed Rune functions that
// take and return structs by value.
#include <cstddef>
#include <cstdint>
#include <cstdio>

struct Wide { double x, y, w, h; };                  // 32 bytes
struct Packed { uint8_t a; int32_t b; uint16_t c; int64_t d; };
enum Colour { Red = 1, Green = 2, Blue = 4 };
struct Value {                                       // a tagged union
    int32_t tag;                                     // 0 Int, 1 Real, 2 Empty
    union { int64_t i; double d; } u;
};

extern "C" double rune_conv_area(Wide r);            // Rune, @export
extern "C" Wide rune_conv_grow(Wide r, double by);   // Rune, @export

extern "C" int64_t rune_conv_layout(void) {
    // Packed: 1, pad 3, 4, 2, pad 6, 8 => offsets 0 4 8 16, size 24.
    return (int64_t)offsetof(Packed, b) * 1000000 + offsetof(Packed, c) * 10000 +
           offsetof(Packed, d) * 100 + sizeof(Packed);
}

extern "C" int64_t rune_conv_packed(Packed p) { return p.a + p.b + p.c + p.d; }

extern "C" int64_t rune_conv_packed_at(const Packed *p) { return p->a + p->b + p->c + p->d; }

extern "C" int32_t rune_conv_colour(Colour c) { return (int32_t)c * 10; }

extern "C" int64_t rune_conv_value(Value v) {
    switch (v.tag) {
    case 0: return v.u.i;
    case 1: return (int64_t)(v.u.d * 10);
    default: return -1;
    }
}

extern "C" Value rune_conv_make(int64_t n) {
    Value v; v.tag = 0; v.u.i = n; return v;
}

extern "C" void rune_conv_call_back(void) {
    Wide w = rune_conv_grow(Wide{0, 0, 3, 5}, 1);
    std::printf("from C: grown %g %g %g %g, area %g\n", w.x, w.y, w.w, w.h,
                rune_conv_area(Wide{0, 0, 3, 5}));
    std::fflush(stdout);
}
