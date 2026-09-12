/*===- rune_runtime.c - The Rune language runtime ------------------------===*/

/* `rand_s` is the C runtime's own entropy source on Windows, and only
 * appears in <stdlib.h> when asked for before the first include. */
#ifdef _WIN32
#define _CRT_RAND_S
#endif

#include "rune_runtime.h"

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#if defined(__APPLE__) || defined(__GLIBC__)
#include <execinfo.h>
#endif

/*===--------------------------------------------------------------------===*\
|* Reference counting
\*===--------------------------------------------------------------------===*/

/* Live-object accounting is cheap and only read at shutdown, so it stays on
 * unconditionally; --safety=full is what decides whether main reports it. */

/* Set RUNE_DEBUG_RC=1 to hunt reference-counting mistakes: freed objects are
 * poisoned and kept, so touching one afterwards reports exactly which type was
 * involved instead of corrupting whatever reused the memory. */
#define RUNE_POISON ((int64_t)0xDEAD0000DEAD0000LL)

static int rc_debug(void) {
  static int cached = -1;
  if (cached < 0) {
    const char *v = getenv("RUNE_DEBUG_RC");
    cached = (v && *v && *v != '0') ? 1 : 0;
  }
  return cached;
}

static void check_not_poisoned(const RuneObject *o, const char *what) {
  if (o->refcount == RUNE_POISON) {
    fprintf(stderr,
            "\n\x1b[1;31m●\x1b[0m rune: %s of an already-destroyed %s object\n",
            what, o->type && o->type->name ? o->type->name : "<anonymous>");
    abort();
  }
}





/*===--------------------------------------------------------------------===*\
|* Weak references
|*
|* A small open-hash table maps a live object to the list of slots pointing
|* weakly at it. Destroying the object walks that list and NULLs every slot, so
|* a weak reference reads as empty rather than dangling. The table only ever
|* holds objects that actually have weak referrers, so programs that use none
|* pay nothing but a NULL check.
\*===--------------------------------------------------------------------===*/

typedef struct WeakSlot {
  void **Slot;
  struct WeakSlot *Next;
} WeakSlot;

typedef struct WeakEntry {
  void *Target;            /* NULL marks a free bucket */
  WeakSlot *Slots;
  struct WeakEntry *Next;  /* collision chain */
} WeakEntry;

#define RUNE_WEAK_BUCKETS 512
static WeakEntry *g_weak_table[RUNE_WEAK_BUCKETS];

static size_t weak_bucket(const void *target) {
  uintptr_t v = (uintptr_t)target;
  v ^= v >> 16;
  v *= 0x9E3779B97F4A7C15ULL;
  return (size_t)((v >> 32) % RUNE_WEAK_BUCKETS);
}

static WeakEntry *weak_find(void *target) {
  for (WeakEntry *e = g_weak_table[weak_bucket(target)]; e; e = e->Next)
    if (e->Target == target)
      return e;
  return NULL;
}

/* Removes `slot` from whichever target currently lists it. */
static void weak_unregister(void **slot) {
  for (size_t b = 0; b < RUNE_WEAK_BUCKETS; ++b) {
    for (WeakEntry *e = g_weak_table[b]; e; e = e->Next) {
      WeakSlot **link = &e->Slots;
      while (*link) {
        if ((*link)->Slot == slot) {
          WeakSlot *dead = *link;
          *link = dead->Next;
          free(dead);
          return;
        }
        link = &(*link)->Next;
      }
    }
  }
}







/* Called from rune_release just before the object is torn down. */
static void weak_zero_all(void *target) {
  size_t b = weak_bucket(target);
  WeakEntry **link = &g_weak_table[b];
  while (*link) {
    WeakEntry *e = *link;
    if (e->Target != target) {
      link = &e->Next;
      continue;
    }
    for (WeakSlot *s = e->Slots; s;) {
      WeakSlot *next = s->Next;
      *s->Slot = NULL;
      free(s);
      s = next;
    }
    *link = e->Next;
    free(e);
    return;
  }
}





int rune_is_kind_of(const void *obj, const RuneTypeInfo *ti) {
  if (!obj || !ti)
    return 0;
  const RuneTypeInfo *t = ((const RuneObject *)obj)->type;
  while (t) {
    if (t == ti)
      return 1;
    t = t->super;
  }
  return 0;
}

const char *rune_any_type_cstr(const void *obj) {
  const RuneTypeInfo *t = obj ? ((const RuneObject *)obj)->type : NULL;
  return t && t->name ? t->name : "()";
}

int rune_any_is(const void *obj, const RuneTypeInfo *ti) {
  if (!obj || !ti)
    return 0;
  /* The descriptor for a type is one symbol program-wide, so the pointer
     comparison is the answer in every ordinary build. The name comparison
     behind it costs nothing when that succeeds, and keeps the answer right
     if the two ever fail to fold into one — a shared library loaded at run
     time carrying its own copy, say. */
  const RuneTypeInfo *t = ((const RuneObject *)obj)->type;
  while (t) {
    if (t == ti)
      return 1;
    if (t->name && ti->name && strcmp(t->name, ti->name) == 0)
      return 1;
    t = t->super;
  }
  return 0;
}









void rune_report_leaks(void) {
  /* The counter lives in the Rune half of the runtime now. */
  int64_t live = rune_live_object_count();
  if (live > 0)
    fprintf(stderr,
            "rune: warning: %lld reference-counted object(s) still live at "
            "exit\n",
            (long long)live);
}



/*===--------------------------------------------------------------------===*\
|* Panics
\*===--------------------------------------------------------------------===*/

/*===--------------------------------------------------------------------===*\
|* Traceback
|*
|* Only printed for a program built with debug information: the compiler
|* defines `rune_debug_build` from whether `-g` was given.
\*===--------------------------------------------------------------------===*/

extern int rune_debug_build;

/* `_R8std__memT6HandleM5ShapeF5derefG3i64` -> `std::mem::Handle::deref<i64>`,
   undoing what Sema::mangleFunction built. Anything that does not parse is
   left exactly as it came. */
static int rune_demangle(const char *sym, char *out, size_t cap) {
  const char *p = sym;
  size_t n = 0;
  if (!p || p[0] != '_' || p[1] != 'R')
    return 0;
  p += 2;
#define PUT(str, len)                                                          \
  do {                                                                         \
    size_t l_ = (len);                                                         \
    if (n + l_ + 1 >= cap) return 0;                                           \
    memcpy(out + n, (str), l_);                                                \
    n += l_;                                                                   \
  } while (0)

  char section = 0;
  int wroteAny = 0, inArgs = 0;
  for (;;) {
    if (*p >= 'A' && *p <= 'Z') {
      section = *p++;
      continue;
    }
    if (*p < '0' || *p > '9')
      break;
    size_t len = 0;
    while (*p >= '0' && *p <= '9')
      len = len * 10 + (size_t)(*p++ - '0');
    if (len == 0 || strlen(p) < len)
      return 0;
    if (section == 'G') {
      PUT(inArgs ? ", " : "<", inArgs ? 2 : 1);
      inArgs = 1;
    } else if (wroteAny) {
      PUT("::", 2);
    }
    /* Module names had their `::` flattened to `_`; put them back. */
    for (size_t i = 0; i < len; ++i) {
      if (section == 0 && p[i] == '_' && i + 1 < len && p[i + 1] == '_') {
        PUT("::", 2);
        ++i;
      } else {
        PUT(p + i, 1);
      }
    }
    wroteAny = 1;
    p += len;
  }
  if (inArgs)
    PUT(">", 1);
  if (!wroteAny || n == 0)
    return 0;
  out[n] = 0;
#undef PUT
  return 1;
}

void rune_traceback(void) {
  if (!rune_debug_build)
    return;
#if defined(__APPLE__) || defined(__GLIBC__)
  void *frames[64];
  int count = backtrace(frames, 64);
  char **names = backtrace_symbols(frames, count);
  if (!names)
    return;
  fputs("\n  \x1b[2mtraceback (most recent call first)\x1b[0m\n", stderr);
  int shown = 0;
  for (int i = 0; i < count; ++i) {
    /* `backtrace_symbols` gives `N  image  addr  symbol + off`; the symbol is
       the second-to-last field on macOS and inside `(...)` on glibc. */
    const char *sym = strstr(names[i], "_R");
    if (!sym)
      continue; /* the runtime's own frames, and anything foreign */
    char plain[512];
    size_t len = strcspn(sym, " +)");
    char raw[512];
    if (len >= sizeof(raw))
      continue;
    memcpy(raw, sym, len);
    raw[len] = 0;
    const char *shownName = rune_demangle(raw, plain, sizeof(plain)) ? plain : raw;
    fprintf(stderr, "    \x1b[2m%2d\x1b[0m %s\n", shown++, shownName);
  }
  if (shown == 0)
    fputs("    \x1b[2m(no Rune frames — the crash is inside foreign code)"
          "\x1b[0m\n", stderr);
  free(names);
#else
  fputs("\n  \x1b[2m(no traceback on this platform)\x1b[0m\n", stderr);
#endif
}

static void panic_header(const char *loc) {
  fflush(stdout);
  fputs("\n\x1b[1;31m●\x1b[0m \x1b[1mruntime panic\x1b[0m", stderr);
  if (loc && *loc)
    fprintf(stderr, " \x1b[2mat %s\x1b[0m", loc);
  fputs("\n  ", stderr);
}

void rune_panic(const char *msg, const char *loc) {
  panic_header(loc);
  fprintf(stderr, "%s\n", msg ? msg : "explicit panic");
  rune_traceback();
  abort();
}

void rune_panic_bounds(int64_t index, int64_t length, const char *loc) {
  panic_header(loc);
  fprintf(stderr, "index %lld is out of bounds for a collection of length %lld\n",
          (long long)index, (long long)length);
  rune_traceback();
  abort();
}

void rune_panic_nil(const char *loc) {
  panic_header(loc);
  fputs("dereferenced a nil reference\n", stderr);
  rune_traceback();
  abort();
}

void rune_panic_div_zero(const char *loc) {
  panic_header(loc);
  fputs("division by zero\n", stderr);
  rune_traceback();
  abort();
}

void rune_panic_overflow(const char *op, const char *loc) {
  panic_header(loc);
  fprintf(stderr, "arithmetic overflow in `%s`\n", op ? op : "operation");
  rune_traceback();
  abort();
}

void rune_panic_unreachable(const char *loc) {
  panic_header(loc);
  fputs("entered unreachable code\n", stderr);
  rune_traceback();
  abort();
}

void rune_panic_no_match(const char *loc) {
  panic_header(loc);
  fputs("no match arm applied to the value\n", stderr);
  rune_traceback();
  abort();
}

void rune_panic_unwrap(const char *what, const char *loc) {
  panic_header(loc);
  fprintf(stderr, "unwrapped an empty %s\n", what ? what : "Option");
  rune_traceback();
  abort();
}

void rune_panic_any(const char *expected, const void *obj, const char *loc) {
  panic_header(loc);
  fprintf(stderr, "this `Any` holds %s, not %s\n", rune_any_type_cstr(obj),
          expected ? expected : "?");
  rune_traceback();
  abort();
}

/*===--------------------------------------------------------------------===*\
|* String
\*===--------------------------------------------------------------------===*/

static void string_deinit(void *self) {
  RuneString *s = (RuneString *)self;
  free(s->data);
  s->data = NULL;
}

static const RuneTypeInfo kStringTypeInfo = {
    "std::String", sizeof(RuneString), string_deinit, NULL, NULL, 0};

static RuneString *string_alloc(int64_t capacity) {
  RuneString *s = (RuneString *)rune_alloc(sizeof(RuneString), &kStringTypeInfo);
  if (capacity < 0)
    capacity = 0;
  s->capacity = capacity;
  s->length = 0;
  s->data = (char *)rune_raw_alloc((uint64_t)capacity + 1);
  s->data[0] = '\0';
  return s;
}

RuneString *rune_string_new(void) { return string_alloc(0); }

RuneString *rune_string_from_bytes(const char *p, int64_t n) {
  if (n < 0)
    n = 0;
  RuneString *s = string_alloc(n);
  if (p && n)
    memcpy(s->data, p, (size_t)n);
  s->length = n;
  s->data[n] = '\0';
  return s;
}

/* One String object per literal, made on first use and shared from then on.
 * `slot` is a per-literal cache the compiler allocates; the object is marked
 * immortal, so it is never freed, never counted, and retain/release on it do
 * nothing. Strings are immutable — every operation returns a new one — so
 * sharing is not observable. */
RuneString *rune_string_literal(const char *p, int64_t n, RuneString **slot) {
  if (slot && *slot)
    return *slot;
  RuneString *s = rune_string_from_bytes(p, n);
  rune_make_immortal(s);
  if (slot)
    *slot = s;
  return s;
}

RuneString *rune_string_from_cstr(const char *p) {
  return rune_string_from_bytes(p, p ? (int64_t)strlen(p) : 0);
}

RuneString *rune_string_copy(const RuneString *s) {
  return s ? rune_string_from_bytes(s->data, s->length) : rune_string_new();
}

RuneString *rune_string_concat(const RuneString *a, const RuneString *b) {
  int64_t la = a ? a->length : 0, lb = b ? b->length : 0;
  RuneString *s = string_alloc(la + lb);
  if (la)
    memcpy(s->data, a->data, (size_t)la);
  if (lb)
    memcpy(s->data + la, b->data, (size_t)lb);
  s->length = la + lb;
  s->data[s->length] = '\0';
  return s;
}

RuneString *rune_string_substring(const RuneString *s, int64_t start, int64_t end,
                                  const char *loc) {
  int64_t len = s ? s->length : 0;
  if (start < 0 || end > len || start > end)
    rune_panic_bounds(start < 0 ? start : end, len, loc);
  return rune_string_from_bytes(s->data + start, end - start);
}

RuneString *rune_string_repeat(const RuneString *s, int64_t times) {
  if (times < 0)
    times = 0;
  int64_t l = s ? s->length : 0;
  RuneString *out = string_alloc(l * times);
  for (int64_t i = 0; i < times; ++i)
    memcpy(out->data + i * l, s->data, (size_t)l);
  out->length = l * times;
  out->data[out->length] = '\0';
  return out;
}

int64_t rune_string_length(const RuneString *s) { return s ? s->length : 0; }

int64_t rune_string_char_count(const RuneString *s) {
  if (!s)
    return 0;
  int64_t n = 0;
  for (int64_t i = 0; i < s->length; ++i)
    if (((unsigned char)s->data[i] & 0xC0) != 0x80)
      ++n;
  return n;
}

const char *rune_string_cstr(const RuneString *s) { return s ? s->data : ""; }

int rune_string_equal(const RuneString *a, const RuneString *b) {
  if (a == b)
    return 1;
  if (!a || !b || a->length != b->length)
    return 0;
  return memcmp(a->data, b->data, (size_t)a->length) == 0;
}

int rune_string_compare(const RuneString *a, const RuneString *b) {
  int64_t la = a ? a->length : 0, lb = b ? b->length : 0;
  int64_t n = la < lb ? la : lb;
  int c = n ? memcmp(a->data, b->data, (size_t)n) : 0;
  if (c)
    return c < 0 ? -1 : 1;
  return la == lb ? 0 : (la < lb ? -1 : 1);
}

/* Mixes one value into a running hash. splitmix64's finaliser: cheap, and it
 * scatters small integers well, which matters because most keys are small
 * integers or short strings. */
uint64_t rune_hash_mix(uint64_t acc, uint64_t v) {
  acc ^= v + 0x9e3779b97f4a7c15ULL + (acc << 6) + (acc >> 2);
  acc ^= acc >> 30;
  acc *= 0xbf58476d1ce4e5b9ULL;
  acc ^= acc >> 27;
  acc *= 0x94d049bb133111ebULL;
  acc ^= acc >> 31;
  return acc;
}

/* FNV-1a over a NUL-terminated run of bytes, so a CString hashes its contents
 * rather than its address. */
uint64_t rune_cstring_hash(const char *p) {
  uint64_t h = 1469598103934665603ULL;
  if (!p)
    return h;
  while (*p) {
    h ^= (uint64_t)(unsigned char)*p++;
    h *= 1099511628211ULL;
  }
  return h;
}

uint64_t rune_string_hash(const RuneString *s) {
  /* FNV-1a */
  uint64_t h = 1469598103934665603ULL;
  if (!s)
    return h;
  for (int64_t i = 0; i < s->length; ++i) {
    h ^= (unsigned char)s->data[i];
    h *= 1099511628211ULL;
  }
  return h;
}

uint8_t rune_string_byte_at(const RuneString *s, int64_t i, const char *loc) {
  int64_t len = s ? s->length : 0;
  if (i < 0 || i >= len)
    rune_panic_bounds(i, len, loc);
  return (uint8_t)s->data[i];
}

/* `i` counts characters, not bytes: `.$at(2)` is the third character however
   wide the ones before it were. `.$byteAt` is the byte-indexed one. */
uint32_t rune_string_char_at(const RuneString *s, int64_t i, int64_t *next,
                             const char *loc) {
  int64_t bytes = s ? s->length : 0;
  if (i < 0)
    rune_panic_bounds(i, rune_string_char_count(s), loc);
  /* Walk `i` characters in. */
  int64_t at = 0;
  for (int64_t seen = 0; seen < i; ++seen) {
    if (at >= bytes)
      rune_panic_bounds(i, rune_string_char_count(s), loc);
    unsigned char c = (unsigned char)s->data[at];
    at += c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
  }
  if (at >= bytes)
    rune_panic_bounds(i, rune_string_char_count(s), loc);
  i = at;
  int64_t len = bytes;
  unsigned char lead = (unsigned char)s->data[i];
  int extra = lead < 0x80 ? 0 : lead < 0xE0 ? 1 : lead < 0xF0 ? 2 : 3;
  if (i + extra >= len)
    extra = 0;
  uint32_t cp;
  switch (extra) {
  case 0: cp = lead; break;
  case 1: cp = ((lead & 0x1Fu) << 6) | ((unsigned char)s->data[i + 1] & 0x3Fu); break;
  case 2:
    cp = ((lead & 0x0Fu) << 12) | (((unsigned char)s->data[i + 1] & 0x3Fu) << 6) |
         ((unsigned char)s->data[i + 2] & 0x3Fu);
    break;
  default:
    cp = ((lead & 0x07u) << 18) | (((unsigned char)s->data[i + 1] & 0x3Fu) << 12) |
         (((unsigned char)s->data[i + 2] & 0x3Fu) << 6) |
         ((unsigned char)s->data[i + 3] & 0x3Fu);
    break;
  }
  if (next)
    *next = i + extra + 1;
  return cp;
}

int64_t rune_string_find(const RuneString *hay, const RuneString *needle) {
  if (!hay || !needle || needle->length == 0)
    return 0;
  if (needle->length > hay->length)
    return -1;
  for (int64_t i = 0; i + needle->length <= hay->length; ++i)
    if (memcmp(hay->data + i, needle->data, (size_t)needle->length) == 0)
      return i;
  return -1;
}

RuneString *rune_string_from_i64(int64_t v) {
  char buf[32];
  int n = snprintf(buf, sizeof(buf), "%lld", (long long)v);
  return rune_string_from_bytes(buf, n);
}

RuneString *rune_string_from_u64(uint64_t v) {
  char buf[32];
  int n = snprintf(buf, sizeof(buf), "%llu", (unsigned long long)v);
  return rune_string_from_bytes(buf, n);
}

/* `{:.3}` — a fixed number of places after the point, rounded the way the C
 * library rounds, which is the way a reader expects. Doing this in Rune with
 * scaled integers would round differently at the edges and overflow early. */
RuneString *rune_string_from_f64_fixed(double v, int64_t places) {
  if (places < 0) places = 0;
  if (places > 30) places = 30;
  /* Enough for the widest double in fixed notation plus the places asked for. */
  char buf[384];
  int n = snprintf(buf, sizeof(buf), "%.*f", (int)places, v);
  if (n < 0) n = 0;
  if (n > (int)sizeof(buf) - 1) n = (int)sizeof(buf) - 1;
  return rune_string_from_bytes(buf, n);
}

RuneString *rune_string_from_f64(double v) {
  char buf[64];
  /* Shortest representation that reads back as the same double. */
  int n = snprintf(buf, sizeof(buf), "%.17g", v);
  for (int prec = 1; prec <= 17; ++prec) {
    char tmp[64];
    int m = snprintf(tmp, sizeof(tmp), "%.*g", prec, v);
    if (strtod(tmp, NULL) == v) {
      memcpy(buf, tmp, (size_t)m + 1);
      n = m;
      break;
    }
  }

  /* %g reaches for exponent notation early ("2e+01" for 20). For magnitudes a
   * reader would rather see written out, re-render in fixed notation and trim
   * the trailing zeros. */
  int hasExponent = 0, hasPoint = 0, isSpecial = 0;
  for (int i = 0; i < n; ++i) {
    if (buf[i] == 'e' || buf[i] == 'E') hasExponent = 1;
    if (buf[i] == '.') hasPoint = 1;
    if (buf[i] == 'n' || buf[i] == 'i') isSpecial = 1; /* nan, inf */
  }

  double magnitude = v < 0 ? -v : v;
  if (hasExponent && !isSpecial && (magnitude == 0.0 ||
                                    (magnitude >= 1e-6 && magnitude < 1e17))) {
    for (int prec = 1; prec <= 17; ++prec) {
      char tmp[64];
      int m = snprintf(tmp, sizeof(tmp), "%.*f", prec, v);
      if (strtod(tmp, NULL) == v) {
        /* Drop trailing zeros, but keep one digit after the point. */
        while (m > 2 && tmp[m - 1] == '0' && tmp[m - 2] != '.')
          tmp[--m] = '\0';
        memcpy(buf, tmp, (size_t)m + 1);
        n = m;
        hasPoint = 1;
        hasExponent = 0;
        break;
      }
    }
  }

  /* A whole number still reads as a float. */
  if (!hasPoint && !hasExponent && !isSpecial && n + 2 < (int)sizeof(buf)) {
    buf[n++] = '.';
    buf[n++] = '0';
    buf[n] = '\0';
  }
  return rune_string_from_bytes(buf, n);
}

RuneString *rune_string_from_bool(int8_t v) {
  return rune_string_from_cstr(v ? "true" : "false");
}

RuneString *rune_string_from_char(uint32_t cp) {
  char buf[4];
  int n = 0;
  if (cp < 0x80) {
    buf[n++] = (char)cp;
  } else if (cp < 0x800) {
    buf[n++] = (char)(0xC0 | (cp >> 6));
    buf[n++] = (char)(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    buf[n++] = (char)(0xE0 | (cp >> 12));
    buf[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
    buf[n++] = (char)(0x80 | (cp & 0x3F));
  } else {
    buf[n++] = (char)(0xF0 | (cp >> 18));
    buf[n++] = (char)(0x80 | ((cp >> 12) & 0x3F));
    buf[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
    buf[n++] = (char)(0x80 | (cp & 0x3F));
  }
  return rune_string_from_bytes(buf, n);
}

RuneString *rune_string_from_ptr(const void *p) {
  char buf[32];
  int n = snprintf(buf, sizeof(buf), "0x%llx", (unsigned long long)(uintptr_t)p);
  return rune_string_from_bytes(buf, n);
}

int64_t rune_string_to_i64(const RuneString *s, int8_t *ok) {
  if (!s || s->length == 0) {
    if (ok) *ok = 0;
    return 0;
  }
  char *end = NULL;
  long long v = strtoll(s->data, &end, 10);
  if (ok)
    *ok = (end && *end == '\0') ? 1 : 0;
  return (int64_t)v;
}

double rune_string_to_f64(const RuneString *s, int8_t *ok) {
  if (!s || s->length == 0) {
    if (ok) *ok = 0;
    return 0;
  }
  char *end = NULL;
  double v = strtod(s->data, &end);
  if (ok)
    *ok = (end && *end == '\0') ? 1 : 0;
  return v;
}

/*===--------------------------------------------------------------------===*\
|* I/O
\*===--------------------------------------------------------------------===*/

void rune_print(const RuneString *s) {
  if (s && s->length)
    fwrite(s->data, 1, (size_t)s->length, stdout);
}

void rune_println(const RuneString *s) {
  rune_print(s);
  fputc('\n', stdout);
}

void rune_eprint(const RuneString *s) {
  if (s && s->length)
    fwrite(s->data, 1, (size_t)s->length, stderr);
}

void rune_eprintln(const RuneString *s) {
  rune_eprint(s);
  fputc('\n', stderr);
}

void rune_print_cstr(const char *s) {
  if (s)
    fputs(s, stdout);
}

RuneString *rune_read_line_checked(int8_t *more) {
  size_t cap = 128, len = 0;
  char *buf = (char *)rune_raw_alloc(cap);
  int c;
  int saw_any = 0;
  while ((c = fgetc(stdin)) != EOF && c != '\n') {
    saw_any = 1;
    if (len + 1 >= cap) {
      cap *= 2;
      buf = (char *)rune_raw_realloc(buf, cap);
    }
    buf[len++] = (char)c;
  }
  /* A line was read when anything at all arrived, terminator included: an
     empty line ends with '\n' and is a line, while end of input with nothing
     before it is not. Without this the two are the same empty string, and a
     loop over standard input cannot tell where to stop. */
  if (more)
    *more = (int8_t)(saw_any || c == '\n');
  RuneString *s = rune_string_from_bytes(buf, (int64_t)len);
  rune_raw_free(buf);
  return s;
}

RuneString *rune_read_line(void) {
  int8_t more = 0;
  return rune_read_line_checked(&more);
}

/*===--------------------------------------------------------------------===*\
|* Process
\*===--------------------------------------------------------------------===*/

static int32_t g_argc = 0;
static char **g_argv = NULL;

void rune_runtime_init(int32_t argc, char **argv) {
  g_argc = argc;
  g_argv = argv;
}

int64_t rune_arg_count(void) { return g_argc; }

RuneString *rune_arg_at(int64_t i) {
  if (i < 0 || i >= g_argc)
    return rune_string_new();
  return rune_string_from_cstr(g_argv[i]);
}

void rune_exit(int32_t code) {
  fflush(stdout);
  exit(code);
}

/*===--------------------------------------------------------------------===*\
|* Unicode
|*
|* Lookups into the generated tables. The normalisation algorithms themselves
|* live in `std::text`, written in Rune; these are only the questions it asks.
\*===--------------------------------------------------------------------===*/

static int64_t ucd_find2(const uint32_t table[][2], uint32_t count,
                         int64_t key) {
  uint32_t lo = 0, hi = count;
  while (lo < hi) {
    uint32_t mid = lo + (hi - lo) / 2;
    if ((int64_t)table[mid][0] == key)
      return (int64_t)table[mid][1];
    if ((int64_t)table[mid][0] < key)
      lo = mid + 1;
    else
      hi = mid;
  }
  return -1;
}

int64_t rune_ucd_combining_class(int64_t cp) {
  int64_t v = ucd_find2(rune_ucd_ccc, rune_ucd_ccc_count, cp);
  return v < 0 ? 0 : v;
}

int64_t rune_ucd_fold_case(int64_t cp) {
  int64_t v = ucd_find2(rune_ucd_fold, rune_ucd_fold_count, cp);
  return v < 0 ? cp : v;
}

int64_t rune_ucd_decompose(int64_t cp, int64_t *first, int64_t *second) {
  uint32_t lo = 0, hi = rune_ucd_decomp_count;
  while (lo < hi) {
    uint32_t mid = lo + (hi - lo) / 2;
    int64_t at = (int64_t)rune_ucd_decomp[mid][0];
    if (at == cp) {
      *first = (int64_t)rune_ucd_decomp[mid][1];
      *second = (int64_t)rune_ucd_decomp[mid][2];
      return *second ? 2 : 1;
    }
    if (at < cp)
      lo = mid + 1;
    else
      hi = mid;
  }
  return 0;
}

int64_t rune_ucd_compose(int64_t first, int64_t second) {
  uint32_t lo = 0, hi = rune_ucd_comp_count;
  while (lo < hi) {
    uint32_t mid = lo + (hi - lo) / 2;
    int64_t a = (int64_t)rune_ucd_comp[mid][0];
    int64_t b = (int64_t)rune_ucd_comp[mid][1];
    if (a == first && b == second)
      return (int64_t)rune_ucd_comp[mid][2];
    if (a < first || (a == first && b < second))
      lo = mid + 1;
    else
      hi = mid;
  }
  return 0;
}

/*===--------------------------------------------------------------------===*\
|* Files
|*
|* Only the part that has to know about `errno`. Everything else about files
|* lives in `std::io`, written in Rune.
\*===--------------------------------------------------------------------===*/

int64_t rune_last_file_error(void) {
  switch (errno) {
  case ENOENT:  return 1;
  case EACCES:  return 2;
  case EPERM:   return 2;
  case EEXIST:  return 3;
  case EISDIR:  return 4;
  case EMFILE:  return 5;
  case ENFILE:  return 5;
  default:      return 0;
  }
}

/*===--------------------------------------------------------------------===*\
|* Directories
|*
|* The one file operation whose shape genuinely differs between platforms.
|* POSIX hands back a `struct dirent` whose useful fields are not portable;
|* Windows has no `dirent` at all and wants a `*` pattern appended to the
|* path. Both are hidden behind the same four calls, so `std::io` above can be
|* written once.
\*===--------------------------------------------------------------------===*/

#ifdef _WIN32

#include <windows.h>

typedef struct RuneDir {
  HANDLE find;
  WIN32_FIND_DATAA data;
  int first;      /* FindFirstFile already produced an entry */
  int done;
} RuneDir;

void *rune_dir_open(const char *path) {
  if (!path) return NULL;
  size_t n = strlen(path);
  /* room for the path, a separator, the wildcard and the terminator */
  char *pattern = (char *)malloc(n + 3);
  if (!pattern) return NULL;
  memcpy(pattern, path, n);
  size_t at = n;
  if (at > 0 && pattern[at - 1] != '\\' && pattern[at - 1] != '/')
    pattern[at++] = '\\';
  pattern[at++] = '*';
  pattern[at] = '\0';

  RuneDir *d = (RuneDir *)calloc(1, sizeof(RuneDir));
  if (!d) { free(pattern); return NULL; }
  d->find = FindFirstFileA(pattern, &d->data);
  free(pattern);
  if (d->find == INVALID_HANDLE_VALUE) {
    free(d);
    errno = ENOENT;
    return NULL;
  }
  d->first = 1;
  return d;
}

const char *rune_dir_next(void *dir) {
  RuneDir *d = (RuneDir *)dir;
  if (!d || d->done) return NULL;
  for (;;) {
    if (!d->first) {
      if (!FindNextFileA(d->find, &d->data)) { d->done = 1; return NULL; }
    }
    d->first = 0;
    const char *name = d->data.cFileName;
    if (name[0] == '.' && (name[1] == '\0' ||
                           (name[1] == '.' && name[2] == '\0')))
      continue;
    return name;
  }
}

void rune_dir_close(void *dir) {
  RuneDir *d = (RuneDir *)dir;
  if (!d) return;
  if (d->find != INVALID_HANDLE_VALUE) FindClose(d->find);
  free(d);
}

int64_t rune_path_is_dir(const char *path) {
  if (!path) return 0;
  DWORD a = GetFileAttributesA(path);
  if (a == INVALID_FILE_ATTRIBUTES) return 0;
  return (a & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
}

#else

#include <dirent.h>
#include <sys/stat.h>

void *rune_dir_open(const char *path) {
  if (!path) return NULL;
  return opendir(path);
}

const char *rune_dir_next(void *dir) {
  DIR *d = (DIR *)dir;
  if (!d) return NULL;
  for (;;) {
    struct dirent *e = readdir(d);
    if (!e) return NULL;
    const char *name = e->d_name;
    if (name[0] == '.' && (name[1] == '\0' ||
                           (name[1] == '.' && name[2] == '\0')))
      continue;
    return name;
  }
}

void rune_dir_close(void *dir) {
  if (dir) closedir((DIR *)dir);
}

int64_t rune_path_is_dir(const char *path) {
  struct stat st;
  if (!path || stat(path, &st) != 0) return 0;
  return S_ISDIR(st.st_mode) ? 1 : 0;
}

#endif

/*==========================================================================*
 * Threads
 *
 * The thinnest possible layer over whatever the platform calls a thread.
 * Everything about *what* may cross a thread boundary is decided by the
 * compiler, from the types; by the time control reaches here the question has
 * been settled and all that is left is to start something and wait for it.
 *
 * Two backings, because the platforms genuinely differ: pthreads everywhere,
 * and the Win32 calls on Windows, where mingw's pthread shim is not always
 * there and `sysconf` never is.
 *==========================================================================*/

#ifdef _WIN32

#include <windows.h>

typedef struct RuneThread {
  HANDLE id;
  void *(*entry)(void *);
  void *argument;
  void *result;
  int joined;
} RuneThread;

static DWORD WINAPI rune_thread_trampoline(LPVOID raw) {
  RuneThread *t = (RuneThread *)raw;
  t->result = t->entry(t->argument);
  return 0;
}

void *rune_thread_start(void *(*entry)(void *), void *argument) {
  RuneThread *t = (RuneThread *)calloc(1, sizeof(RuneThread));
  if (!t)
    return NULL;
  t->entry = entry;
  t->argument = argument;
  t->id = CreateThread(NULL, 0, rune_thread_trampoline, t, 0, NULL);
  if (!t->id) {
    free(t);
    return NULL;
  }
  return t;
}

void *rune_thread_join(void *handle) {
  RuneThread *t = (RuneThread *)handle;
  if (!t)
    return NULL;
  if (!t->joined) {
    WaitForSingleObject(t->id, INFINITE);
    t->joined = 1;
  }
  return t->result;
}

void rune_thread_dispose(void *handle) {
  RuneThread *t = (RuneThread *)handle;
  if (!t)
    return;
  if (t->id)
    CloseHandle(t->id);
  t->id = NULL;
  free(t);
}

int64_t rune_hardware_threads(void) {
  SYSTEM_INFO info;
  GetSystemInfo(&info);
  return info.dwNumberOfProcessors > 0 ? (int64_t)info.dwNumberOfProcessors : 1;
}

void rune_thread_yield(void) { SwitchToThread(); }

void *rune_mutex_new(void) {
  CRITICAL_SECTION *m = (CRITICAL_SECTION *)calloc(1, sizeof(CRITICAL_SECTION));
  if (m)
    InitializeCriticalSection(m);
  return m;
}

void rune_mutex_lock(void *m) {
  if (m) EnterCriticalSection((CRITICAL_SECTION *)m);
}

void rune_mutex_unlock(void *m) {
  if (m) LeaveCriticalSection((CRITICAL_SECTION *)m);
}

void rune_mutex_dispose(void *m) {
  if (!m) return;
  DeleteCriticalSection((CRITICAL_SECTION *)m);
  free(m);
}

/* A critical section is already recursive, which is what the weak table
 * needs: releasing an object while holding the lock can reach it again. */
static CRITICAL_SECTION g_weak_lock;
static INIT_ONCE g_weak_lock_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK rune_weak_lock_init(PINIT_ONCE o, PVOID p, PVOID *c) {
  (void)o; (void)p; (void)c;
  InitializeCriticalSection(&g_weak_lock);
  return TRUE;
}

void rune_weak_lock(void) {
  InitOnceExecuteOnce(&g_weak_lock_once, rune_weak_lock_init, NULL, NULL);
  EnterCriticalSection(&g_weak_lock);
}

void rune_weak_unlock(void) { LeaveCriticalSection(&g_weak_lock); }

void *rune_cond_new(void) {
  CONDITION_VARIABLE *c =
      (CONDITION_VARIABLE *)calloc(1, sizeof(CONDITION_VARIABLE));
  if (c)
    InitializeConditionVariable(c);
  return c;
}

/* Waits on `c`, releasing `m` while asleep and holding it again on return —
 * which is the whole point of a condition variable. */
void rune_cond_wait(void *c, void *m) {
  if (c && m)
    SleepConditionVariableCS((CONDITION_VARIABLE *)c, (CRITICAL_SECTION *)m,
                             INFINITE);
}

void rune_cond_signal(void *c) {
  if (c) WakeConditionVariable((CONDITION_VARIABLE *)c);
}

void rune_cond_broadcast(void *c) {
  if (c) WakeAllConditionVariable((CONDITION_VARIABLE *)c);
}

void rune_cond_dispose(void *c) { free(c); }

void rune_thread_sleep_ns(int64_t nanos) {
  if (nanos <= 0) return;
  /* Sleep takes whole milliseconds, so anything shorter rounds up to one
   * rather than to nothing: a sleep that does not sleep is a spin. */
  DWORD ms = (DWORD)((nanos + 999999) / 1000000);
  Sleep(ms ? ms : 1);
}

int64_t rune_monotonic_ns(void) {
  LARGE_INTEGER freq, now;
  if (!QueryPerformanceFrequency(&freq) || freq.QuadPart == 0)
    return (int64_t)GetTickCount64() * 1000000;
  QueryPerformanceCounter(&now);
  /* Split the division so a long uptime cannot overflow the multiply. */
  int64_t whole = now.QuadPart / freq.QuadPart;
  int64_t rest = now.QuadPart % freq.QuadPart;
  return whole * 1000000000 + (rest * 1000000000) / freq.QuadPart;
}

#else /* pthreads */

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <unistd.h>

typedef struct RuneThread {
  pthread_t id;
  void *(*entry)(void *);
  void *argument;
  void *result;
  int joined;
} RuneThread;

static void *rune_thread_trampoline(void *raw) {
  RuneThread *t = (RuneThread *)raw;
  t->result = t->entry(t->argument);
  return NULL;
}

void *rune_thread_start(void *(*entry)(void *), void *argument) {
  RuneThread *t = (RuneThread *)calloc(1, sizeof(RuneThread));
  if (!t)
    return NULL;
  t->entry = entry;
  t->argument = argument;
  if (pthread_create(&t->id, NULL, rune_thread_trampoline, t) != 0) {
    free(t);
    return NULL;
  }
  return t;
}

void *rune_thread_join(void *handle) {
  RuneThread *t = (RuneThread *)handle;
  if (!t)
    return NULL;
  if (!t->joined) {
    pthread_join(t->id, NULL);
    t->joined = 1;
  }
  return t->result;
}

void rune_thread_dispose(void *handle) {
  RuneThread *t = (RuneThread *)handle;
  if (!t)
    return;
  if (!t->joined)
    pthread_detach(t->id);
  free(t);
}

int64_t rune_hardware_threads(void) {
  long n = sysconf(_SC_NPROCESSORS_ONLN);
  return n > 0 ? (int64_t)n : 1;
}

void rune_thread_yield(void) { sched_yield(); }

void *rune_mutex_new(void) {
  pthread_mutex_t *m = (pthread_mutex_t *)calloc(1, sizeof(pthread_mutex_t));
  if (m && pthread_mutex_init(m, NULL) != 0) {
    free(m);
    return NULL;
  }
  return m;
}

void rune_mutex_lock(void *m) {
  if (m) pthread_mutex_lock((pthread_mutex_t *)m);
}

void rune_mutex_unlock(void *m) {
  if (m) pthread_mutex_unlock((pthread_mutex_t *)m);
}

void rune_mutex_dispose(void *m) {
  if (!m) return;
  pthread_mutex_destroy((pthread_mutex_t *)m);
  free(m);
}

/* The weak table is the one piece of runtime state every thread shares, so it
 * gets a lock of its own. Recursive, because releasing an object while
 * holding it can reach it again. */
static pthread_mutex_t g_weak_lock;
static pthread_once_t g_weak_lock_once = PTHREAD_ONCE_INIT;

static void rune_weak_lock_init(void) {
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&g_weak_lock, &attr);
  pthread_mutexattr_destroy(&attr);
}

void rune_weak_lock(void) {
  pthread_once(&g_weak_lock_once, rune_weak_lock_init);
  pthread_mutex_lock(&g_weak_lock);
}

void rune_weak_unlock(void) { pthread_mutex_unlock(&g_weak_lock); }

void *rune_cond_new(void) {
  pthread_cond_t *c = (pthread_cond_t *)calloc(1, sizeof(pthread_cond_t));
  if (c && pthread_cond_init(c, NULL) != 0) {
    free(c);
    return NULL;
  }
  return c;
}

/* Waits on `c`, releasing `m` while asleep and holding it again on return —
 * which is the whole point of a condition variable. */
void rune_cond_wait(void *c, void *m) {
  if (c && m)
    pthread_cond_wait((pthread_cond_t *)c, (pthread_mutex_t *)m);
}

void rune_cond_signal(void *c) {
  if (c) pthread_cond_signal((pthread_cond_t *)c);
}

void rune_cond_broadcast(void *c) {
  if (c) pthread_cond_broadcast((pthread_cond_t *)c);
}

void rune_cond_dispose(void *c) {
  if (!c) return;
  pthread_cond_destroy((pthread_cond_t *)c);
  free(c);
}

void rune_thread_sleep_ns(int64_t nanos) {
  if (nanos <= 0) return;
  struct timespec want;
  want.tv_sec = (time_t)(nanos / 1000000000);
  want.tv_nsec = (long)(nanos % 1000000000);
  /* A signal can cut a sleep short; finish what is left rather than
   * returning early, which is almost never what the caller meant. */
  struct timespec left;
  while (nanosleep(&want, &left) != 0 && errno == EINTR)
    want = left;
}

int64_t rune_monotonic_ns(void) {
  struct timespec now;
#if defined(CLOCK_MONOTONIC)
  clock_gettime(CLOCK_MONOTONIC, &now);
#else
  clock_gettime(CLOCK_REALTIME, &now);
#endif
  return (int64_t)now.tv_sec * 1000000000 + (int64_t)now.tv_nsec;
}

#endif

/*==========================================================================*
 * Sockets
 *
 * The whole of the platform's opinion about networking, in one place. What a
 * `struct sockaddr_in` is laid out like differs between macOS, Linux and
 * Windows — macOS puts a length byte where Linux puts half the address
 * family — so `std::net` never sees one. It works in host names, ports and
 * descriptors, and everything below is what turns those into calls.
 *
 * Descriptors cross the boundary as `int64_t`: a POSIX one is an `int`, but a
 * Windows `SOCKET` is pointer-sized, and one spelling that fits both is
 * simpler than two that nearly do.
 *==========================================================================*/

#ifdef _WIN32

#include <winsock2.h>
#include <ws2tcpip.h>

typedef SOCKET rune_socket;
#define RUNE_BAD_SOCKET INVALID_SOCKET
#define rune_closesocket closesocket
#define rune_sockerrno WSAGetLastError()
typedef int rune_socklen;
typedef char rune_optval;

void rune_net_start(void) {
  static LONG started = 0;
  if (InterlockedCompareExchange(&started, 1, 0) != 0)
    return;
  WSADATA data;
  WSAStartup(MAKEWORD(2, 2), &data);
}

static int64_t rune_net_classify(int err) {
  switch (err) {
  case WSAECONNREFUSED: return 1;
  case WSAEADDRINUSE:   return 2;
  case WSAENETUNREACH:
  case WSAEHOSTUNREACH: return 3;
  case WSAEWOULDBLOCK:  return 4;
  case WSAEINTR:        return 5;
  case WSAETIMEDOUT:    return 6;
  case WSAECONNRESET:   return 7;
  case WSAEACCES:       return 8;
  default:              return 0;
  }
}

#else

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

typedef int rune_socket;
#define RUNE_BAD_SOCKET (-1)
#define rune_closesocket close
#define rune_sockerrno errno
typedef socklen_t rune_socklen;
typedef void rune_optval;

void rune_net_start(void) {}

static int64_t rune_net_classify(int err) {
  switch (err) {
  case ECONNREFUSED: return 1;
  case EADDRINUSE:   return 2;
  case ENETUNREACH:
  case EHOSTUNREACH: return 3;
#if EAGAIN != EWOULDBLOCK
  case EWOULDBLOCK:  return 4;
#endif
  case EAGAIN:       return 4;
  case EINTR:        return 5;
  case ETIMEDOUT:    return 6;
  case ECONNRESET:
  case EPIPE:        return 7;
  case EACCES:
  case EPERM:        return 8;
  default:           return 0;
  }
}

#endif

/* The last socket error, remembered per thread: a program that reads it after
 * a failed call must not be told about another thread's failure instead. */
#if defined(_WIN32)
static __declspec(thread) int64_t g_net_error = 0;
#else
static __thread int64_t g_net_error = 0;
#endif

static void rune_net_remember(void) {
  g_net_error = rune_net_classify(rune_sockerrno);
}

int64_t rune_net_last_error(void) { return g_net_error; }

/* Resolves `host`:`port` to the first address that answers. A NULL or empty
 * host means "every interface" when `passive`, and the loopback otherwise. */
static struct addrinfo *rune_net_resolve(const char *host, int32_t port,
                                         int passive) {
  char service[16];
  snprintf(service, sizeof(service), "%d", (int)port);

  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  if (passive)
    hints.ai_flags = AI_PASSIVE;

  const char *node = (host && *host) ? host : NULL;
  struct addrinfo *found = NULL;
  if (getaddrinfo(node, service, &hints, &found) != 0) {
    g_net_error = 9;      /* not found */
    return NULL;
  }
  return found;
}

int64_t rune_net_listen(const char *host, int32_t port, int32_t backlog) {
  rune_net_start();
  struct addrinfo *addrs = rune_net_resolve(host, port, 1);
  if (!addrs)
    return -1;

  int64_t out = -1;
  for (struct addrinfo *a = addrs; a; a = a->ai_next) {
    rune_socket s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
    if (s == RUNE_BAD_SOCKET) {
      rune_net_remember();
      continue;
    }
    /* Without this a listener cannot be restarted until the kernel has let
     * go of the port, which makes every edit-run cycle wait. */
    int on = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (rune_optval *)&on, sizeof(on));
    if (bind(s, a->ai_addr, (rune_socklen)a->ai_addrlen) != 0 ||
        listen(s, backlog > 0 ? backlog : 128) != 0) {
      rune_net_remember();
      rune_closesocket(s);
      continue;
    }
    out = (int64_t)s;
    break;
  }
  freeaddrinfo(addrs);
  return out;
}

int64_t rune_net_connect(const char *host, int32_t port) {
  rune_net_start();
  struct addrinfo *addrs = rune_net_resolve(host, port, 0);
  if (!addrs)
    return -1;

  int64_t out = -1;
  for (struct addrinfo *a = addrs; a; a = a->ai_next) {
    rune_socket s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
    if (s == RUNE_BAD_SOCKET) {
      rune_net_remember();
      continue;
    }
    if (connect(s, a->ai_addr, (rune_socklen)a->ai_addrlen) != 0) {
      rune_net_remember();
      rune_closesocket(s);
      continue;
    }
    out = (int64_t)s;
    break;
  }
  freeaddrinfo(addrs);
  return out;
}

/* Writes the printable form of `addr` into `out`, and its port into `port`. */
static void rune_net_describe_peer(const struct sockaddr_storage *addr,
                                   char *out, int64_t cap, int32_t *port) {
  if (out && cap > 0)
    out[0] = 0;
  if (port)
    *port = 0;
  if (addr->ss_family == AF_INET) {
    const struct sockaddr_in *v4 = (const struct sockaddr_in *)addr;
    if (out && cap > 0)
      inet_ntop(AF_INET, (void *)&v4->sin_addr, out, (rune_socklen)cap);
    if (port)
      *port = (int32_t)ntohs(v4->sin_port);
  } else if (addr->ss_family == AF_INET6) {
    const struct sockaddr_in6 *v6 = (const struct sockaddr_in6 *)addr;
    if (out && cap > 0)
      inet_ntop(AF_INET6, (void *)&v6->sin6_addr, out, (rune_socklen)cap);
    if (port)
      *port = (int32_t)ntohs(v6->sin6_port);
  }
}

int64_t rune_net_accept(int64_t fd, char *peer_out, int64_t peer_cap,
                        int32_t *peer_port) {
  struct sockaddr_storage addr;
  rune_socklen len = (rune_socklen)sizeof(addr);
  memset(&addr, 0, sizeof(addr));
  rune_socket s = accept((rune_socket)fd, (struct sockaddr *)&addr, &len);
  if (s == RUNE_BAD_SOCKET) {
    rune_net_remember();
    return -1;
  }
  rune_net_describe_peer(&addr, peer_out, peer_cap, peer_port);
  return (int64_t)s;
}

int64_t rune_net_read(int64_t fd, void *buffer, int64_t count) {
  if (count <= 0)
    return 0;
#ifdef _WIN32
  int n = recv((rune_socket)fd, (char *)buffer, (int)count, 0);
#else
  ssize_t n = recv((rune_socket)fd, buffer, (size_t)count, 0);
#endif
  if (n < 0) {
    rune_net_remember();
    return -1;
  }
  return (int64_t)n;
}

int64_t rune_net_write(int64_t fd, const void *data, int64_t count) {
  if (count <= 0)
    return 0;
  /* MSG_NOSIGNAL where it exists: a write to a closed connection should come
   * back as an error, not kill the process with SIGPIPE. macOS says the same
   * thing with a socket option, set in rune_net_connect's callers. */
#if defined(MSG_NOSIGNAL)
  ssize_t n = send((rune_socket)fd, data, (size_t)count, MSG_NOSIGNAL);
#elif defined(_WIN32)
  int n = send((rune_socket)fd, (const char *)data, (int)count, 0);
#else
  ssize_t n = send((rune_socket)fd, data, (size_t)count, 0);
#endif
  if (n < 0) {
    rune_net_remember();
    return -1;
  }
  return (int64_t)n;
}

int32_t rune_net_shutdown(int64_t fd, int32_t how) {
#ifdef _WIN32
  int which = how == 0 ? SD_RECEIVE : (how == 1 ? SD_SEND : SD_BOTH);
#else
  int which = how == 0 ? SHUT_RD : (how == 1 ? SHUT_WR : SHUT_RDWR);
#endif
  if (shutdown((rune_socket)fd, which) != 0) {
    rune_net_remember();
    return -1;
  }
  return 0;
}

int32_t rune_net_close(int64_t fd) {
  if (fd < 0)
    return 0;
  if (rune_closesocket((rune_socket)fd) != 0) {
    rune_net_remember();
    return -1;
  }
  return 0;
}

int32_t rune_net_set_nodelay(int64_t fd, int32_t on) {
  int value = on ? 1 : 0;
  if (setsockopt((rune_socket)fd, IPPROTO_TCP, TCP_NODELAY,
                 (rune_optval *)&value, sizeof(value)) != 0) {
    rune_net_remember();
    return -1;
  }
  return 0;
}

int32_t rune_net_set_timeout_ms(int64_t fd, int64_t ms, int32_t for_read) {
  int which = for_read ? SO_RCVTIMEO : SO_SNDTIMEO;
#ifdef _WIN32
  DWORD value = (DWORD)(ms < 0 ? 0 : ms);
  if (setsockopt((rune_socket)fd, SOL_SOCKET, which, (const char *)&value,
                 sizeof(value)) != 0) {
#else
  struct timeval value;
  value.tv_sec = (time_t)(ms / 1000);
  value.tv_usec = (suseconds_t)((ms % 1000) * 1000);
  if (setsockopt((rune_socket)fd, SOL_SOCKET, which, &value,
                 sizeof(value)) != 0) {
#endif
    rune_net_remember();
    return -1;
  }
  return 0;
}

int32_t rune_net_local_port(int64_t fd) {
  struct sockaddr_storage addr;
  rune_socklen len = (rune_socklen)sizeof(addr);
  memset(&addr, 0, sizeof(addr));
  if (getsockname((rune_socket)fd, (struct sockaddr *)&addr, &len) != 0) {
    rune_net_remember();
    return -1;
  }
  int32_t port = 0;
  rune_net_describe_peer(&addr, NULL, 0, &port);
  return port;
}

/*==========================================================================*
 * Environment, entropy and the wall clock
 *
 * The small pieces `std::env`, `std::random` and `std::time`'s calendar need
 * from the operating system, and nothing more: a variable read or written,
 * a few unpredictable bytes, the time of day, and what the local zone thinks
 * the offset from UTC is at a given moment. The calendar arithmetic itself
 * is in Rune, where it can be read.
 *==========================================================================*/

#include <time.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <unistd.h>
#if defined(__APPLE__)
#include <sys/random.h>
#endif
extern char **environ;
#endif

/* A NUL-terminated copy of a RuneString's bytes, for the calls that need
 * one. `rune_string_cstr` hands back a pointer that stays valid while the
 * string does, which is all these need. */

RuneString *rune_env_get(const char *name) {
  const char *v = name ? getenv(name) : NULL;
  return v ? rune_string_from_cstr(v) : rune_string_new();
}

int8_t rune_env_has(const char *name) {
  return name && getenv(name) != NULL;
}

int8_t rune_env_set(const char *name, const char *value) {
  if (!name || !*name || !value)
    return 0;
#ifdef _WIN32
  return _putenv_s(name, value) == 0;
#else
  return setenv(name, value, 1) == 0;
#endif
}

int8_t rune_env_remove(const char *name) {
  if (!name || !*name)
    return 0;
#ifdef _WIN32
  return _putenv_s(name, "") == 0;
#else
  return unsetenv(name) == 0;
#endif
}

/* The whole environment as `NAME=value` entries, one per index. On Windows
 * the C runtime's `_environ` mirrors the process block well enough. */
int64_t rune_env_count(void) {
  int64_t n = 0;
#ifdef _WIN32
  char **env = _environ;
#else
  char **env = environ;
#endif
  if (!env)
    return 0;
  while (env[n])
    ++n;
  return n;
}

RuneString *rune_env_at(int64_t index) {
#ifdef _WIN32
  char **env = _environ;
#else
  char **env = environ;
#endif
  if (!env || index < 0)
    return rune_string_new();
  for (int64_t i = 0; i < index; ++i)
    if (!env[i])
      return rune_string_new();
  return env[index] ? rune_string_from_cstr(env[index]) : rune_string_new();
}

RuneString *rune_current_directory(void) {
  char buf[4096];
#ifdef _WIN32
  if (!_getcwd(buf, (int)sizeof(buf)))
    return rune_string_new();
#else
  if (!getcwd(buf, sizeof(buf)))
    return rune_string_new();
#endif
  return rune_string_from_cstr(buf);
}

int8_t rune_set_current_directory(const char *path) {
  if (!path)
    return 0;
#ifdef _WIN32
  return _chdir(path) == 0;
#else
  return chdir(path) == 0;
#endif
}

/* Fills `out` with `count` bytes the operating system considers
 * unpredictable. Returns 0 if it could not, so the caller can fall back to
 * something rather than seed from zeros. */
int8_t rune_os_entropy(uint8_t *out, int64_t count) {
  if (!out || count <= 0)
    return 0;
#ifdef _WIN32
  /* Four bytes at a time from the C runtime, which needs no extra library. */
  for (int64_t i = 0; i < count;) {
    unsigned int v;
    if (rand_s(&v) != 0)
      return 0;
    for (int b = 0; b < 4 && i < count; ++b, ++i)
      out[i] = (uint8_t)(v >> (8 * b));
  }
  return 1;
#elif defined(__APPLE__) || defined(__OpenBSD__) || defined(__FreeBSD__)
  arc4random_buf(out, (size_t)count);
  return 1;
#else
  FILE *f = fopen("/dev/urandom", "rb");
  if (!f)
    return 0;
  size_t got = fread(out, 1, (size_t)count, f);
  fclose(f);
  return got == (size_t)count;
#endif
}

/* Seconds since 1970-01-01T00:00:00Z, and the nanoseconds within that
 * second, from the wall clock — which, unlike the monotonic one, can be set
 * and can jump. */
int64_t rune_wall_clock_ns(void) {
#ifdef _WIN32
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  /* 100ns ticks since 1601-01-01; 11644473600 seconds to 1970. */
  uint64_t ticks = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
  return (int64_t)(ticks - 116444736000000000ULL) * 100;
#else
  struct timespec now;
  clock_gettime(CLOCK_REALTIME, &now);
  return (int64_t)now.tv_sec * 1000000000 + (int64_t)now.tv_nsec;
#endif
}

/* The local zone's offset from UTC, in seconds, at the instant `unix_seconds`
 * — positive east of Greenwich. Daylight saving is whatever the platform says
 * it was at that moment. */
int64_t rune_local_utc_offset(int64_t unix_seconds) {
  time_t t = (time_t)unix_seconds;
  struct tm local;
  struct tm utc;
#ifdef _WIN32
  /* Windows' `localtime` and `gmtime` hand back per-thread storage, so a
   * copy is as safe as the `_r` forms elsewhere. */
  struct tm *lp = localtime(&t);
  if (!lp)
    return 0;
  local = *lp;
  struct tm *up = gmtime(&t);
  if (!up)
    return 0;
  utc = *up;
#else
  if (!localtime_r(&t, &local) || !gmtime_r(&t, &utc))
    return 0;
#endif
  /* Difference in civil time, which is the offset by definition. */
  int64_t ldays = (int64_t)local.tm_yday + 365 * (int64_t)local.tm_year;
  int64_t udays = (int64_t)utc.tm_yday + 365 * (int64_t)utc.tm_year;
  int64_t days = ldays - udays;
  /* A year boundary between the two views shows up as ±364/365 days. */
  if (days > 1)
    days = 1;
  if (days < -1)
    days = -1;
  int64_t lsec = (int64_t)local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec;
  int64_t usec = (int64_t)utc.tm_hour * 3600 + utc.tm_min * 60 + utc.tm_sec;
  return days * 86400 + (lsec - usec);
}

/* The name the platform gives the local zone right now — "BST", "PDT" —
 * or an empty string if it will not say. */
RuneString *rune_local_zone_name(void) {
  time_t t = time(NULL);
  struct tm local;
#ifdef _WIN32
  struct tm *lp = localtime(&t);
  if (!lp)
    return rune_string_new();
  local = *lp;
  char buf[64];
  if (strftime(buf, sizeof(buf), "%Z", &local) == 0)
    return rune_string_new();
  return rune_string_from_cstr(buf);
#else
  if (!localtime_r(&t, &local))
    return rune_string_new();
  return rune_string_from_cstr(local.tm_zone ? local.tm_zone : "");
#endif
}
