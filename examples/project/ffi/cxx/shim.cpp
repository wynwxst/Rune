// The C++ half of the `ffi` package.
//
// Where `c/shim.c` is what an `extern "C"` block talks to, this is what an
// `extern "C++"` block talks to: namespaced functions, a class with a
// constructor and a destructor, a const member, a static factory, a template,
// and structs of several shapes — the ones a platform's C++ ABI passes
// differently from one another, which is the whole reason the compiler has to
// know the rules rather than guess.
//
// Nothing here is written for Rune's benefit. It is ordinary C++, compiled by
// an ordinary C++ compiler, and the Rune side matches it by declaration.

namespace shim {

struct Vec2 { double x, y; };
struct Pair { int a, b; };
struct Wide { long a, b, c; };

double lengthSq(Vec2 v) { return v.x * v.x + v.y * v.y; }
Vec2 scaled(Vec2 v, double k) { return {v.x * k, v.y * k}; }
Pair swapped(Pair p) { return {p.b, p.a}; }
Wide tripled(Wide w) { return {w.a * 3, w.b * 3, w.c * 3}; }
int sumRef(const Pair &p) { return p.a + p.b; }
void bump(int &x, int by) { x += by; }

template <typename T> struct Span { const T *data; unsigned long len; };
long total(Span<long> s) {
    long t = 0;
    for (unsigned long i = 0; i < s.len; ++i) t += s.data[i];
    return t;
}
double totalD(Span<double> s) {
    double t = 0;
    for (unsigned long i = 0; i < s.len; ++i) t += s.data[i];
    return t;
}

enum Colour { Red = 1, Green = 2, Blue = 4 };
Colour brighter(Colour c) { return c == Red ? Green : c == Green ? Blue : Blue; }

/// How many `Counter`s are alive: what the destructor is for, and what the
/// Rune tests check to see that it ran.
int liveCounters = 0;

class Counter {
public:
    Counter(int start);
    ~Counter();
    int next();
    int peek() const;
    void setStep(int step);
    Pair state() const;
    static Counter *make(int start);
    static void destroy(Counter *c);
private:
    int value_;
    int step_;
};

Counter::Counter(int start) : value_(start), step_(1) { ++liveCounters; }
Counter::~Counter() { --liveCounters; }
int Counter::next() { int v = value_; value_ += step_; return v; }
int Counter::peek() const { return value_; }
void Counter::setStep(int step) { step_ = step; }
Pair Counter::state() const { return {value_, step_}; }
Counter *Counter::make(int start) { return new Counter(start); }
void Counter::destroy(Counter *c) { delete c; }

/// Single, non-virtual inheritance: `this` is one address for both halves,
/// so a pointer to the derived class is a pointer to the base.
class Stepper : public Counter {
public:
    Stepper(int start, int step);
    int twice();
};

Stepper::Stepper(int start, int step) : Counter(start) { setStep(step); }
int Stepper::twice() { next(); return next(); }

} // namespace shim
