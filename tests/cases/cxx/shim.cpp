// The C++ half of 94_cxx_shim.rune: one of every shape a declaration can
// take, so the Rune side can check that each symbol is spelled right and
// each value crosses the calling convention intact. Compiled by the test
// runner with the build's own C++ compiler; also built with GCC for MinGW
// when the example package under examples/project/ffi is cross-tested.
#include <cstdint>
#include <cstddef>
#include <cstdio>
namespace geo {
struct Pair { int a; int b; };
struct Vec2 { double x; double y; };
struct Big { long a, b, c; };
struct Mixed { int i; double d; };
struct Bytes3 { unsigned char a, b, c; };
struct SR { const char *p; size_t n; };
enum Colour { Red = 1, Green = 2, Blue = 4 };

int add(int a, int b) { return a + b; }
long addLong(long a, long b) { return a + b; }
unsigned long long big(unsigned long long v) { return v * 2; }
double scaleD(double v, float f) { return v * f; }
bool flip(bool b) { return !b; }
Pair swap(Pair p) { return {p.b, p.a}; }
Vec2 scale(Vec2 v, double k) { return {v.x*k, v.y*k}; }
Big triple(Big b) { return {b.a*3,b.b*3,b.c*3}; }
Mixed mix(Mixed m) { return {m.i+1, m.d+1}; }
Bytes3 inc3(Bytes3 b) { return {(unsigned char)(b.a+1),(unsigned char)(b.b+1),(unsigned char)(b.c+1)}; }
size_t srlen(SR s) { return s.n; }
SR mksr(const char *p) { SR s; s.p = p; s.n = 0; while (p[s.n]) s.n++; return s; }
Colour next(Colour c) { return c == Red ? Green : c == Green ? Blue : Red; }
int colourValue(Colour c) { return (int)c; }
int sumRef(const Pair &p) { return p.a + p.b; }
void bump(int &x) { x += 10; }
int callback(int (*f)(int, int), int a, int b) { return f(a, b); }
int counter = 7;

class Counter {
public:
  Counter(int start);
  ~Counter();
  int next();
  int peek() const;
  void setStep(int s);
  static Counter *make(int s);
  static void destroy(Counter *c);
  Pair pair() const;
  Big bigOf() const;
  int operator[](int i) const;
  static void *operator new(size_t n, int tag);
  static void operator delete(void *p);
private:
  int value;
  int step;
};
Counter::Counter(int start) : value(start), step(1) { printf("Counter(%d)\n", start); }
Counter::~Counter() { printf("~Counter(%d)\n", value); }
int Counter::next() { int v = value; value += step; return v; }
int Counter::peek() const { return value; }
void Counter::setStep(int s) { step = s; }
Counter *Counter::make(int s) { return new (0) Counter(s); }
void Counter::destroy(Counter *c) { delete c; }
Pair Counter::pair() const { return {value, step}; }
Big Counter::bigOf() const { return {value, step, value + step}; }
int Counter::operator[](int i) const { return value + i * step; }
void Counter::operator delete(void *p) { ::operator delete(p); }
void *Counter::operator new(size_t n, int tag) { printf("operator new(%zu, %d)\n", n, tag); return ::operator new(n); }

class Named : public Counter {
public:
  Named(int start, const char *name);
  const char *name() const;
private:
  const char *name_;
};
Named::Named(int start, const char *name) : Counter(start), name_(name) {}
const char *Named::name() const { return name_; }

template <typename T> struct Span { const T *data; size_t len; };
long sumSpan(Span<long> s) { long t = 0; for (size_t i = 0; i < s.len; ++i) t += s.data[i]; return t; }
double sumSpanD(Span<double> s) { double t = 0; for (size_t i = 0; i < s.len; ++i) t += s.data[i]; return t; }
}
