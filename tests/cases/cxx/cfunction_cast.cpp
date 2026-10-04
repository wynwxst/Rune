// The foreign half of `B7_cfunction_cast_struct`: C calling a Rune function
// it was given through an explicit `as @cfunction(...)` cast, with structs
// too big for registers — passed by pointer on arm64, on the stack on x86-64.
typedef struct { int kind; int x; const void *d[3]; } Cur;
typedef int (*Visit)(Cur a, Cur b, void *data);
extern "C" int rune_cfc_drive(Visit v, void *data) {
    Cur a = {7, 8, {(void *)1, (void *)2, (void *)3}};
    Cur b = {9, 10, {(void *)4, (void *)5, (void *)6}};
    return v(a, b, data);
}
