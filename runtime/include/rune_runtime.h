/*===- rune_runtime.h - The Rune language runtime ------------------------===*\
|*
|* Everything the compiler emits calls into is declared here. The runtime is
|* deliberately small and dependency-free: reference counting, panics, the
|* String object and a handful of I/O helpers.
|*
|* Object layout
|* -------------
|* Every heap object allocated by Rune starts with a RuneObject header. Class
|* instances, String and closure environments all share this layout so a single
|* pair of retain/release routines serves them all.
|*
\*===--------------------------------------------------------------------===*/
#ifndef RUNE_RUNTIME_H
#define RUNE_RUNTIME_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*RuneDeinitFn)(void *self);
/* A deep copy of an object, as a new object with a count of 1. */
typedef void *(*RuneCloneFn)(const void *self);

/* Static per-type metadata emitted once per class/struct that needs it. */
typedef struct RuneTypeInfo {
  const char *name;        /* fully qualified, e.g. "app::Point"          */
  uint64_t size;           /* allocation size in bytes, header included    */
  RuneDeinitFn deinit;     /* user `deinit` + field releases; may be NULL  */
  const struct RuneTypeInfo *super; /* base class, or NULL                 */
  const void *const *vtable;        /* virtual method table, or NULL       */
  uint32_t vtableCount;
  /* Copies an object of exactly this type. Filled in under single ownership,
     where a copy made through a base class, a `dyn` or an `Any` has to be of
     the object's own type; NULL under counting, which shares instead. */
  RuneCloneFn clone;
} RuneTypeInfo;

/* Header shared by every reference-counted allocation. */
typedef struct RuneObject {
  int64_t refcount;
  const RuneTypeInfo *type;
} RuneObject;

/*--- Reference counting -------------------------------------------------*/

/* Allocates `size` zeroed bytes with a refcount of 1. */
void *rune_alloc(uint64_t size, const RuneTypeInfo *ti);
/* Increments the refcount; NULL-safe. Returns `obj` for convenient chaining. */
void *rune_retain(void *obj);
/* Decrements the refcount, running `deinit` and freeing at zero. NULL-safe. */
void rune_release(void *obj);
/* Current strong count, for tests and debugging. */
int64_t rune_refcount(void *obj);

/* The teardown queue: `enter` says whether to destroy the object now or
 * leave it to the teardown already running on this thread, `next` hands back
 * what was queued, and `leave` ends the outermost one. See the comment beside
 * their definitions. */
int rune_teardown_enter(void *obj);
void *rune_teardown_next(void);
void rune_teardown_leave(void);
/* A deep copy of `obj` made by its own type's `clone`; NULL-safe. Panics
 * when the type has none — a closure's captures, say. */
void *rune_clone_object(const void *obj);
/* True if `obj` is an instance of `ti` or one of its subclasses. */
int rune_is_kind_of(const void *obj, const RuneTypeInfo *ti);

/*--- Any ----------------------------------------------------------------*/

/* An `Any` is the boxed value itself: one pointer, whose object header names
 * the type it was built from. These are what `is`, `holds` and `typeName`
 * lower onto. */

/* The name of the type `obj` actually has; never NULL. */
const char *rune_any_type_cstr(const void *obj);
/* Like rune_is_kind_of, but also accepts a descriptor that matches by name.
 * That is what makes the answer independent of how the program was linked. */
int rune_any_is(const void *obj, const RuneTypeInfo *ti);

/*--- Weak references ----------------------------------------------------*/

/* A weak reference does not keep its target alive. The runtime remembers every
 * slot that holds one, and writes NULL into all of them when the object is
 * destroyed, so a weak field can never be read as a dangling pointer.
 *
 * `rune_weak_store` replaces whatever `slot` held; passing NULL unregisters it.
 * `rune_weak_load` reads the slot, returning NULL once the target has died. */
void rune_weak_store(void **slot, void *target);
void *rune_weak_load(void **slot);
/* Forgets `slot` without writing to it; used when the holder itself dies. */
void rune_weak_clear(void **slot);

/* Raw, non-refcounted allocation used for array storage and FFI buffers. */
void *rune_raw_alloc(uint64_t size);
/* The size a block was allocated with travels with it: the freestanding
   runtime hands it to the program's @deallocator, which keeps no header. */
void *rune_raw_realloc(void *p, uint64_t old_size, uint64_t size);
void rune_raw_free(void *p, uint64_t size);
/// Copies `size` bytes; the blocks must not overlap.
void rune_raw_copy(void *to, const void *from, uint64_t size);

/*--- Threads ------------------------------------------------------------*/

/* What may cross a thread boundary is decided by the compiler, from the
 * types. These only start something and wait for it. */
void *rune_thread_start(void *(*entry)(void *), void *argument);
void *rune_thread_join(void *handle);
void rune_thread_dispose(void *handle);
int64_t rune_hardware_threads(void);
void rune_thread_yield(void);
void rune_thread_sleep_ns(int64_t nanos);
/// A clock that only goes forwards, for measuring how long something took.
int64_t rune_monotonic_ns(void);

void *rune_mutex_new(void);
void rune_mutex_lock(void *m);
void rune_mutex_unlock(void *m);
void rune_mutex_dispose(void *m);

/* A condition variable: how a thread waits for something another will do. */
void *rune_cond_new(void);
void rune_cond_wait(void *c, void *m);
/* The same, giving up after `nanos` if nothing has signalled by then. */
void rune_cond_wait_ns(void *c, void *m, int64_t nanos);
void rune_cond_signal(void *c);
void rune_cond_broadcast(void *c);
void rune_cond_dispose(void *c);

/* The weak table is the one piece of runtime state every thread shares. */
void rune_weak_lock(void);
void rune_weak_unlock(void);

/*--- Tasks --------------------------------------------------------------*/

/* What `std::task` runs on: stacks that can stop in the middle of a call and
 * be picked up again, and a per-thread executor that keeps the ready ones.
 * See rune_task.c. */
void *rune_task_new(void (*entry)(void *), void *payload);
void rune_task_start(void *task);
/* 0: done with a value; 1: the waiter was cancelled; 2: the task ended
 * without a value. */
int64_t rune_task_wait(void *task);
int64_t rune_task_wait_any(void *tasks, int64_t count);
int64_t rune_task_is_done(void *task);
void rune_task_yield_now(void);
void *rune_task_timer(int64_t nanos);
void *rune_task_io(int64_t fd, int64_t events);
void *rune_task_external(void);
void *rune_task_manual(void);
void rune_task_complete(void *task);
void rune_task_complete_remote(void *task);
void rune_task_wait_posted(void *task);
void rune_task_cancel(void *task);
int64_t rune_task_is_cancelled(void *task);
int64_t rune_task_exited_cancelled(void *task);
void rune_task_suspend_enter(void);
int64_t rune_task_suspend_leave(void);
void rune_task_free(void *task);
int64_t rune_task_in_task(void);
int64_t rune_task_stack_size(void);
void rune_task_set_stack_size(int64_t bytes);
/* Worker threads for jobs that would otherwise hold up an executor. */
void rune_pool_submit(void *(*entry)(void *), void *argument);
int64_t rune_pool_size(void);

/*--- Panics -------------------------------------------------------------*/

/* All of these print a diagnostic to stderr and abort. `loc` is a
 * "file:line:col" string baked in by the code generator. */
void rune_panic(const char *msg, const char *loc);
/// Prints a stack traceback, but only when the program was built with `-g`.
void rune_traceback(void);

/*--- Files ---------------------------------------------------------------*/

/* Why the last file operation failed, classified so callers do not have to
   know what `errno` is on this platform:
     0 other, 1 not found, 2 permission denied, 3 already exists,
     4 is a directory, 5 too many open files                              */
int64_t rune_last_file_error(void);

/*--- Directories ---------------------------------------------------------*/

/* Listing a directory is the one file operation whose shape genuinely differs
   between platforms: POSIX hands back a `struct dirent` whose fields are not
   portable, and Windows has no `dirent` at all. So the whole of it lives here
   and `std::io` sees four calls that behave the same everywhere.

   Opens `path` for listing. Null when it cannot be read; the reason is in
   `rune_last_file_error`. */
void *rune_dir_open(const char *path);
/* The next entry's name, or null when there are no more. `.` and `..` are
   never returned. The pointer stays valid until the next call on the same
   handle, which is why the caller copies it. */
const char *rune_dir_next(void *dir);
/* Closes a handle from `rune_dir_open`. Null is accepted and ignored. */
void rune_dir_close(void *dir);
/* 1 when `path` names a directory, 0 when it is anything else or missing. */
int64_t rune_path_is_dir(const char *path);

/*--- Unicode -------------------------------------------------------------*/

/* The generated tables, and the four questions the normaliser asks of them.
   Hangul is arithmetic and handled by the caller, not by these. */
extern const uint32_t rune_ucd_ccc[][2];
extern const uint32_t rune_ucd_ccc_count;
extern const uint32_t rune_ucd_decomp[][3];
extern const uint32_t rune_ucd_decomp_count;
extern const uint32_t rune_ucd_comp[][3];
extern const uint32_t rune_ucd_comp_count;
extern const uint32_t rune_ucd_fold[][2];
extern const uint32_t rune_ucd_fold_count;

/* Canonical combining class; 0 for a starter. */
int64_t rune_ucd_combining_class(int64_t cp);
/* Canonical decomposition. Writes up to two code points and returns how many
   (0 when the character does not decompose). */
int64_t rune_ucd_decompose(int64_t cp, int64_t *first, int64_t *second);
/* The character `first` and `second` compose to, or 0. */
int64_t rune_ucd_compose(int64_t first, int64_t second);
/* Simple case fold: one code point to one, or the input unchanged. */
int64_t rune_ucd_fold_case(int64_t cp);
void rune_panic_bounds(int64_t index, int64_t length, const char *loc);
void rune_panic_nil(const char *loc);
void rune_panic_div_zero(const char *loc);
void rune_panic_overflow(const char *op, const char *loc);
void rune_panic_unreachable(const char *loc);
/* Raised when a `match` has no arm for the scrutinee. */
void rune_panic_no_match(const char *loc);
/* Raised by Option::unwrap / Result::unwrap on the empty case. */
void rune_panic_unwrap(const char *what, const char *loc);
/* Raised by Any::expect when the value inside is some other type. */
void rune_panic_any(const char *expected, const void *obj, const char *loc);

/*--- String -------------------------------------------------------------*/

/* UTF-8, reference counted, NUL-terminated for cheap FFI hand-off. */
typedef struct RuneString {
  RuneObject header;
  int64_t length;   /* bytes, excluding the NUL */
  int64_t capacity;
  char *data;
} RuneString;

RuneString *rune_string_new(void);
RuneString *rune_string_from_cstr(const char *s);
RuneString *rune_string_from_bytes(const char *p, int64_t n);
/* Interned literal: one immortal object per literal, cached in *slot. */
RuneString *rune_string_literal(const char *p, int64_t n, RuneString **slot);
void rune_make_immortal(void *obj);
RuneString *rune_string_copy(const RuneString *s);
RuneString *rune_string_concat(const RuneString *a, const RuneString *b);
RuneString *rune_string_substring(const RuneString *s, int64_t start, int64_t end,
                                  const char *loc);
RuneString *rune_string_repeat(const RuneString *s, int64_t times);
int64_t rune_string_length(const RuneString *s);
int64_t rune_string_char_count(const RuneString *s);
const char *rune_string_cstr(const RuneString *s);
int rune_string_equal(const RuneString *a, const RuneString *b);
int rune_string_compare(const RuneString *a, const RuneString *b);
uint64_t rune_string_hash(const RuneString *s);
uint64_t rune_hash_mix(uint64_t acc, uint64_t v);
uint64_t rune_cstring_hash(const char *p);
uint8_t rune_string_byte_at(const RuneString *s, int64_t i, const char *loc);
/* Unicode scalar at byte offset `i`; advances `*next` past it. */
uint32_t rune_string_char_after(const RuneString *s, int64_t at, int64_t *next);
uint32_t rune_string_char_at(const RuneString *s, int64_t i, int64_t *next,
                             const char *loc);
int64_t rune_string_find(const RuneString *hay, const RuneString *needle);

/* Conversions used by string interpolation and the `Show` mark. */
RuneString *rune_string_from_i64(int64_t v);
RuneString *rune_string_from_u64(uint64_t v);
RuneString *rune_string_from_f64(double v);
/// `{:.N}`: exactly `places` digits after the point.
RuneString *rune_string_from_f64_fixed(double v, int64_t places);
RuneString *rune_string_from_bool(int8_t v);
RuneString *rune_string_from_char(uint32_t cp);
RuneString *rune_string_from_ptr(const void *p);
int64_t rune_string_to_i64(const RuneString *s, int8_t *ok);
double rune_string_to_f64(const RuneString *s, int8_t *ok);

/*--- I/O ----------------------------------------------------------------*/

void rune_print(const RuneString *s);
void rune_println(const RuneString *s);
void rune_eprint(const RuneString *s);
void rune_eprintln(const RuneString *s);
void rune_print_cstr(const char *s);
/* Reads one line from stdin without the terminator; NULL-terminated string. */
RuneString *rune_read_line(void);
/* The same, but says whether a line was actually read: `*more` is zero at end
   of input, which is what tells an empty line apart from no line at all. */
RuneString *rune_read_line_checked(int8_t *more);
/* Exactly `count` bytes of stdin, however many reads that takes. `*ok` is
   zero when the input ended first; what did arrive is still returned. This is
   what a length-prefixed protocol on stdin needs, where a line reader would
   stop at the first newline inside the payload. */
RuneString *rune_read_stdin_exact(int64_t count, int8_t *ok);
/* Pushes buffered stdout to the operating system. */
void rune_flush_stdout(void);
/* Makes stdin unbuffered; must come before the first read. */
void rune_stdin_unbuffer(void);
/* Whether stdin has input (or its end) within `millis` milliseconds. Only
   meaningful for unbuffered stdin, which the stdio buffer cannot hide. */
int8_t rune_stdin_waiting(int64_t millis);

/*--- Child processes ----------------------------------------------------*/

/* Running another program and collecting what it printed. Built up in steps
   so an argument list never has to cross the boundary as one value: make a
   command, add its arguments one at a time, run it, then read each stream.

   The program is found through PATH when its name has no separator. Both
   streams are read as they arrive, so a child that fills one pipe while the
   other is being waited on cannot deadlock. */
void *rune_command_new(const RuneString *program);
void rune_command_arg(void *cmd, const RuneString *arg);
/* The directory the child starts in; the parent's own when never set. */
void rune_command_directory(void *cmd, const RuneString *dir);
/* The exit status, 128 + the signal when a signal ended it, or -1 when the
   program could not be started at all. */
int64_t rune_command_run(void *cmd);
/* What the child wrote: `which` is 1 for stdout and 2 for stderr. */
RuneString *rune_command_output(void *cmd, int64_t which);
void rune_command_free(void *cmd);

/*--- Process ------------------------------------------------------------*/

void rune_exit(int32_t code);
/* Installed by the generated `main` so panics can flush buffered output. */
void rune_runtime_init(int32_t argc, char **argv);
int64_t rune_arg_count(void);
RuneString *rune_arg_at(int64_t i);

/*--- Environment, entropy and the wall clock ----------------------------*/

/* Empty when the variable is unset or empty; `rune_env_has` tells the two
 * apart. */
RuneString *rune_env_get(const char *name);
int8_t rune_env_has(const char *name);
int8_t rune_env_set(const char *name, const char *value);
int8_t rune_env_remove(const char *name);
/* The environment as `NAME=value` entries. */
int64_t rune_env_count(void);
RuneString *rune_env_at(int64_t index);
/* Empty when it cannot be read. */
RuneString *rune_current_directory(void);
int8_t rune_set_current_directory(const char *path);
/* Unpredictable bytes from the operating system; 0 when it has none. */
int8_t rune_os_entropy(uint8_t *out, int64_t count);
/* Nanoseconds since the Unix epoch, from the wall clock. */
int64_t rune_wall_clock_ns(void);
/* The local zone's offset from UTC at that instant, in seconds. */
int64_t rune_local_utc_offset(int64_t unix_seconds);
RuneString *rune_local_zone_name(void);

/*--- Sockets -------------------------------------------------------------*/

/* The part of networking that has to know what a `struct sockaddr_in` looks
 * like on this machine, and nothing more. Everything about what a connection
 * *is* lives in `std::net`, written in Rune.
 *
 * Every call returns a negative number on failure; `rune_net_last_error`
 * then says which of `std::net::NetError`'s cases it was. Descriptors are
 * `int64_t` because Windows sockets are pointer-sized handles. */

/* Prepares the socket layer. Winsock needs it; everywhere else it does
 * nothing. Calling it more than once is harmless. */
void rune_net_start(void);
/* A listening socket bound to `host`:`port`, or -1. `host` may be empty or
 * "0.0.0.0" for every interface. */
int64_t rune_net_listen(const char *host, int32_t port, int32_t backlog);
/* A connected socket, or -1. `host` may be a name or a dotted address. */
int64_t rune_net_connect(const char *host, int32_t port);
/* Takes the next waiting connection. Writes the peer's address into
 * `peer_out` (at most `peer_cap` bytes, always terminated) and its port into
 * `peer_port`; either may be NULL. */
int64_t rune_net_accept(int64_t fd, char *peer_out, int64_t peer_cap,
                        int32_t *peer_port);
int64_t rune_net_read(int64_t fd, void *buffer, int64_t count);
int64_t rune_net_write(int64_t fd, const void *data, int64_t count);
/* `how`: 0 read, 1 write, 2 both. */
int32_t rune_net_shutdown(int64_t fd, int32_t how);
int32_t rune_net_close(int64_t fd);
/* Nagle off, so a small reply leaves immediately. */
int64_t rune_net_dup(int64_t fd);
int32_t rune_net_set_nonblocking(int64_t fd, int32_t on);
int32_t rune_net_set_nodelay(int64_t fd, int32_t on);
/* 0 clears the timeout. `for_read` picks SO_RCVTIMEO over SO_SNDTIMEO. */
int32_t rune_net_set_timeout_ms(int64_t fd, int64_t ms, int32_t for_read);
/* The local port a socket ended up on, which is what a listener bound to
 * port 0 needs in order to say where it is. */
int32_t rune_net_local_port(int64_t fd);
/* 0 other, 1 refused, 2 in use, 3 unreachable, 4 would block, 5 interrupted,
 * 6 timed out, 7 reset by peer, 8 permission denied, 9 not found. */
int64_t rune_net_last_error(void);

/*--- Leak checking (enabled with --safety=full) --------------------------*/

/* Returns the number of live reference-counted objects. The generated main
 * calls this at exit when leak checking is on. */
int64_t rune_live_object_count(void);
void rune_report_leaks(void);

#ifdef __cplusplus
}
#endif

#endif /* RUNE_RUNTIME_H */
