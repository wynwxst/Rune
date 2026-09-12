/* The C half of the `ffi` package.
 *
 * Everything here is deliberately plain C: scalars, a struct by value, a
 * pointer out-parameter, a NUL-terminated string and a function pointer.
 * Those are the shapes that cross the boundary, and each has a test. */
#include <stdint.h>
#include <string.h>

typedef struct { double x, y; } shim_Point;

/* Structs cross by pointer, not by value. Every ABI agrees on what a pointer
 * to a struct means; they disagree about when a struct itself travels in a
 * register, and Windows x64 disagrees loudest. */
double shim_length_sq(const shim_Point *p) { return p->x * p->x + p->y * p->y; }

void shim_scale(const shim_Point *p, double k, shim_Point *out) {
    out->x = p->x * k;
    out->y = p->y * k;
}

int64_t shim_sum(const int64_t *values, int64_t count) {
    int64_t total = 0;
    for (int64_t i = 0; i < count; ++i) total += values[i];
    return total;
}

/* Two results, so both come back through pointers. */
void shim_divmod(int64_t a, int64_t b, int64_t *quotient, int64_t *remainder) {
    *quotient = a / b;
    *remainder = a % b;
}

/* A callback: C decides how often to call back into Rune. */
int64_t shim_apply(int64_t (*f)(int64_t), int64_t x, int64_t times) {
    for (int64_t i = 0; i < times; ++i) x = f(x);
    return x;
}

const char *shim_name(void) { return "shim"; }

uint64_t shim_length(const char *text) { return (uint64_t)strlen(text); }
