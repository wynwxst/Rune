/*===- rune_task.c - Tasks: stacks that wait --------------------------------===*
 *
 * The mechanism under `std::task`. A task is a piece of Rune code with a
 * stack of its own, which is what lets it stop in the middle of a call and be
 * picked up again later: `.await` on something that is not finished parks the
 * task, and whatever finishes it puts the task back on the queue. Nothing
 * here decides *what* to run — that is the library's business — only how one
 * stack hands the processor to another and how the ones that are ready are
 * found again.
 *
 * Every thread that uses tasks has an executor of its own, and a task belongs
 * to the thread that made it. Two things reach across threads: a timer, which
 * is just a deadline the executor watches, and an "external" task that some
 * other thread completes when its work is done. Both are plain tasks with no
 * stack, so waiting on one is the same as waiting on any other.
 *
 * Switching stacks is the one platform-specific piece. POSIX systems do it
 * with `ucontext`, which is old but everywhere; Windows has fibers, which are
 * the same idea under a different name.
 *
 *===----------------------------------------------------------------------===*/

#if !defined(_WIN32)
#define _XOPEN_SOURCE 700
#define _DARWIN_C_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "rune_runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
/* mingw's inline `GetCurrentFiber` reads the TEB through a null-based
 * pointer, which GCC's bounds check objects to; the header is what it is. */
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <ucontext.h>
#include <unistd.h>
#endif

/* macOS marks `ucontext` deprecated and keeps shipping it, working, on every
 * architecture; it is what portable coroutine libraries fall back to there.
 * The warning says nothing this file can act on. */
#if defined(__APPLE__) && defined(__clang__)
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif

#if defined(_MSC_VER)
#define RUNE_TLS __declspec(thread)
#else
#define RUNE_TLS _Thread_local
#endif

/*===--------------------------------------------------------------------===*\
|* Stacks
\*===--------------------------------------------------------------------===*/

/* Reserved, not committed: a task that touches 20 KB of its stack costs
 * 20 KB, whatever this says. The guard below the stack turns an overflow
 * into a fault rather than a silent write over the next allocation. */
static int64_t g_stack_size = 1024 * 1024;
#define RUNE_STACK_GUARD 16384

typedef struct RuneTask RuneTask;
typedef struct RuneExecutor RuneExecutor;

#ifdef _WIN32

typedef struct RuneFiber {
  LPVOID handle;
} RuneFiber;

#else

/* On the two architectures that matter here the switch is a few dozen
 * instructions of assembly: save the callee-saved registers, swap the stack
 * pointer, restore, return. `ucontext` does the same job everywhere else,
 * at the price of a system call per switch to save the signal mask. */
#if defined(__aarch64__) || (defined(__x86_64__) && !defined(_WIN32))
#define RUNE_CTX_ASM 1
#else
#define RUNE_CTX_ASM 0
#endif

typedef struct RuneFiber {
#if RUNE_CTX_ASM
  void *sp;
#else
  ucontext_t ctx;
#endif
  void *mapping;
  size_t mappingSize;
} RuneFiber;

#if RUNE_CTX_ASM
/* void rune_ctx_switch(void **saveSp, void *loadSp): parks this stack's
 * registers on it, records its stack pointer at `saveSp`, and continues on
 * the stack `loadSp`, which was left by an earlier call to this — or was
 * laid out by `fiber_make` to start a task.
 *
 * void rune_ctx_trampoline(void): what a fresh stack "returns" into. The
 * task pointer rides in a callee-saved register, and the frame pointer is
 * cleared first so a traceback taken inside the task stops at its base. */
void rune_ctx_switch(void **saveSp, void *loadSp);
void rune_ctx_trampoline(void);
void rune_ctx_entry(void *task);

#if defined(__APPLE__)
#define RUNE_SYM(name) "_" #name
#define RUNE_FN_TYPE(name)
#define RUNE_ASM_SECTION ".text\n"
#else
#define RUNE_SYM(name) #name
#define RUNE_FN_TYPE(name) ".type " #name ",@function\n"
#define RUNE_ASM_SECTION ".text\n"
#endif

#if defined(__aarch64__)
__asm__(
    RUNE_ASM_SECTION
    ".globl " RUNE_SYM(rune_ctx_switch) "\n"
    RUNE_FN_TYPE(rune_ctx_switch)
    ".p2align 2\n"
    RUNE_SYM(rune_ctx_switch) ":\n"
    "  sub  sp, sp, #160\n"
    "  stp  x19, x20, [sp, #0]\n"
    "  stp  x21, x22, [sp, #16]\n"
    "  stp  x23, x24, [sp, #32]\n"
    "  stp  x25, x26, [sp, #48]\n"
    "  stp  x27, x28, [sp, #64]\n"
    "  stp  x29, x30, [sp, #80]\n"
    "  stp  d8,  d9,  [sp, #96]\n"
    "  stp  d10, d11, [sp, #112]\n"
    "  stp  d12, d13, [sp, #128]\n"
    "  stp  d14, d15, [sp, #144]\n"
    "  mov  x2, sp\n"
    "  str  x2, [x0]\n"
    "  mov  sp, x1\n"
    "  ldp  x19, x20, [sp, #0]\n"
    "  ldp  x21, x22, [sp, #16]\n"
    "  ldp  x23, x24, [sp, #32]\n"
    "  ldp  x25, x26, [sp, #48]\n"
    "  ldp  x27, x28, [sp, #64]\n"
    "  ldp  x29, x30, [sp, #80]\n"
    "  ldp  d8,  d9,  [sp, #96]\n"
    "  ldp  d10, d11, [sp, #112]\n"
    "  ldp  d12, d13, [sp, #128]\n"
    "  ldp  d14, d15, [sp, #144]\n"
    "  add  sp, sp, #160\n"
    "  ret\n"
    ".globl " RUNE_SYM(rune_ctx_trampoline) "\n"
    RUNE_FN_TYPE(rune_ctx_trampoline)
    ".p2align 2\n"
    RUNE_SYM(rune_ctx_trampoline) ":\n"
    "  mov  x0, x19\n"
    "  mov  x29, #0\n"
    "  mov  x30, #0\n"
    "  bl   " RUNE_SYM(rune_ctx_entry) "\n"
    "  brk  #0\n"
#if !defined(__APPLE__)
    ".section .note.GNU-stack,\"\",%progbits\n"
#endif
);

#define RUNE_CTX_FRAME 160
static void fiber_layout(RuneFiber *f, char *top, void *task) {
  /* What `rune_ctx_switch` will pop: x19 carries the task, x29 and x30 are
   * zero except x30, which "returns" into the trampoline. */
  char *frame = top - RUNE_CTX_FRAME;
  memset(frame, 0, RUNE_CTX_FRAME);
  ((void **)frame)[0] = task;                              /* x19 */
  ((void **)frame)[11] = (void *)rune_ctx_trampoline;      /* x30 */
  f->sp = frame;
}

#elif defined(__x86_64__)
__asm__(
    RUNE_ASM_SECTION
    ".globl " RUNE_SYM(rune_ctx_switch) "\n"
    RUNE_FN_TYPE(rune_ctx_switch)
    ".p2align 4\n"
    RUNE_SYM(rune_ctx_switch) ":\n"
    "  pushq %rbp\n"
    "  pushq %rbx\n"
    "  pushq %r12\n"
    "  pushq %r13\n"
    "  pushq %r14\n"
    "  pushq %r15\n"
    "  movq  %rsp, (%rdi)\n"
    "  movq  %rsi, %rsp\n"
    "  popq  %r15\n"
    "  popq  %r14\n"
    "  popq  %r13\n"
    "  popq  %r12\n"
    "  popq  %rbx\n"
    "  popq  %rbp\n"
    "  ret\n"
    ".globl " RUNE_SYM(rune_ctx_trampoline) "\n"
    RUNE_FN_TYPE(rune_ctx_trampoline)
    ".p2align 4\n"
    RUNE_SYM(rune_ctx_trampoline) ":\n"
    "  movq  %r12, %rdi\n"
    "  xorq  %rbp, %rbp\n"
    "  call  " RUNE_SYM(rune_ctx_entry) "\n"
    "  ud2\n"
#if !defined(__APPLE__)
    ".section .note.GNU-stack,\"\",@progbits\n"
#endif
);

static void fiber_layout(RuneFiber *f, char *top, void *task) {
  /* Below the return address into the trampoline sit the six registers the
   * switch pops, r12 carrying the task. The top is 16-byte aligned, so the
   * trampoline's `call` leaves the stack as the ABI wants it. */
  void **sp = (void **)top;
  *--sp = (void *)rune_ctx_trampoline;   /* popped by `ret` */
  *--sp = NULL;                           /* rbp */
  *--sp = NULL;                           /* rbx */
  *--sp = task;                           /* r12 */
  *--sp = NULL;                           /* r13 */
  *--sp = NULL;                           /* r14 */
  *--sp = NULL;                           /* r15 */
  f->sp = sp;
}
#endif
#endif

/* Stacks are handed back to a small pool rather than to the kernel, because a
 * program that runs many short tasks would otherwise map and unmap the same
 * few hundred kilobytes for each. The pool is per thread, like everything
 * else here. */
#define RUNE_STACK_POOL 32
typedef struct RuneStackPool {
  void *mappings[RUNE_STACK_POOL];
  size_t sizes[RUNE_STACK_POOL];
  size_t count;
} RuneStackPool;

static RUNE_TLS RuneStackPool g_stack_pool;

static void *stack_map(size_t size) {
  for (size_t i = g_stack_pool.count; i-- > 0;) {
    if (g_stack_pool.sizes[i] == size) {
      void *m = g_stack_pool.mappings[i];
      g_stack_pool.mappings[i] = g_stack_pool.mappings[g_stack_pool.count - 1];
      g_stack_pool.sizes[i] = g_stack_pool.sizes[g_stack_pool.count - 1];
      g_stack_pool.count--;
      return m;
    }
  }
  int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#ifdef MAP_NORESERVE
  flags |= MAP_NORESERVE;
#endif
  void *m = mmap(NULL, size, PROT_READ | PROT_WRITE, flags, -1, 0);
  if (m == MAP_FAILED)
    return NULL;
  mprotect(m, RUNE_STACK_GUARD, PROT_NONE);
  return m;
}

static void stack_unmap(void *m, size_t size) {
  if (!m)
    return;
  if (g_stack_pool.count < RUNE_STACK_POOL) {
    g_stack_pool.mappings[g_stack_pool.count] = m;
    g_stack_pool.sizes[g_stack_pool.count] = size;
    g_stack_pool.count++;
    return;
  }
  munmap(m, size);
}

#endif

/*===--------------------------------------------------------------------===*\
|* Tasks and the executor
\*===--------------------------------------------------------------------===*/

enum {
  TASK_NEW,       /* made, never run */
  TASK_READY,     /* on the queue */
  TASK_RUNNING,   /* on the processor, or blocked inside a resume of another */
  TASK_WAITING,   /* parked until something finishes */
  TASK_DONE
};

enum {
  TASK_FIBER,     /* has a stack and an entry */
  TASK_TIMER,     /* done when its deadline passes */
  TASK_EXTERNAL,  /* done when another thread says so */
  TASK_MANUAL,    /* done when code on this thread says so */
  TASK_IO         /* done when a socket is ready */
};

/* What `rune_task_wait` comes back with. */
enum {
  WAIT_DONE = 0,        /* the task finished with a value */
  WAIT_CANCELLED = 1,   /* the *waiter* was cancelled while it waited */
  WAIT_TARGET_GONE = 2  /* the task finished without a value: cancelled */
};

struct RuneTask {
  RuneExecutor *exec;
  int kind;
  int state;
  void (*entry)(void *);
  void *payload;
  RuneFiber fiber;
  int fiberMade;
  /* Who switched to us, so a yield knows where to go back to. */
  RuneTask *resumer;
  RuneTask **waiters;
  size_t waiterCount, waiterCap;
  /* What this task is parked on, so a cancel can take it off again. */
  RuneTask **waitingOn;
  size_t waitingOnCount, waitingOnCap;
  int64_t deadline;
  /* Ready-queue link. */
  RuneTask *next;
  /* Position in the timer heap, so a cancelled timer can be pulled out. */
  size_t heapIndex;
  /* Set when the task was freed from its own stack — the last reference to
   * its future went inside its body — so the stack is torn down by whoever
   * it hands the processor back to, once nothing is running on it. */
  int freeLater;
  /* Cancellation. `cancelRequested` is the ask; `cancelledExit` says the
   * task ended without a value because of it. `interruptible` counts the
   * suspension points the task is inside — a wait may be cut short only
   * from within one. */
  int cancelRequested;
  int cancelledExit;
  int interruptible;
  /* An external task: whether the other thread has posted its completion.
   * Read and written under the executor's lock. */
  int posted;
  /* An I/O task: the socket and what it waits for. */
  int64_t fd;
  int ioEvents;
};

struct RuneExecutor {
  RuneTask *current;
  RuneTask *readyHead, *readyTail;
  RuneTask **timers;
  size_t timerCount, timerCap;
  /* External tasks nobody has completed yet. While there are any, an idle
   * executor waits rather than declaring a deadlock. */
  size_t externalPending;
  /* Completions posted by other threads, taken under the lock. */
  void *lock, *cond;
  RuneTask **remote;
  size_t remoteCount, remoteCap;
  /* Sockets being waited on. */
  RuneTask **io;
  size_t ioCount, ioCap;
  /* How another thread wakes an idle executor: a byte down a pipe, which
   * `poll` watches alongside the sockets. */
#ifdef _WIN32
  SOCKET wake;
  LPVOID rootFiber;
#else
  int wakeRead, wakeWrite;
#if RUNE_CTX_ASM
  void *rootSp;
#else
  ucontext_t root;
#endif
#endif
};

static RUNE_TLS RuneExecutor *g_executor;

static void task_fatal(const char *msg) {
  rune_panic(msg, "std::task");
}

void rune_net_start(void);

#ifndef _WIN32
static pthread_key_t g_executor_key;
static pthread_once_t g_executor_key_once = PTHREAD_ONCE_INIT;

static void executor_destroy(void *raw);

static void executor_key_init(void) {
  pthread_key_create(&g_executor_key, executor_destroy);
}
#endif

/*--- The wake-up pipe ----------------------------------------------------*/

static void wake_open(RuneExecutor *ex) {
#ifdef _WIN32
  /* Windows can `poll` a socket and nothing else, so the wake-up channel is
   * a UDP socket talking to itself on the loopback. */
  rune_net_start();
  SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
  if (s == INVALID_SOCKET)
    task_fatal("cannot make the executor's wake-up socket");
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) != 0)
    task_fatal("cannot bind the executor's wake-up socket");
  int len = sizeof(addr);
  if (getsockname(s, (struct sockaddr *)&addr, &len) != 0 ||
      connect(s, (struct sockaddr *)&addr, sizeof(addr)) != 0)
    task_fatal("cannot connect the executor's wake-up socket");
  u_long on = 1;
  ioctlsocket(s, FIONBIO, &on);
  ex->wake = s;
#else
  int fds[2];
  if (pipe(fds) != 0)
    task_fatal("cannot make the executor's wake-up pipe");
  for (int i = 0; i < 2; ++i) {
    fcntl(fds[i], F_SETFL, fcntl(fds[i], F_GETFL) | O_NONBLOCK);
    fcntl(fds[i], F_SETFD, FD_CLOEXEC);
  }
  ex->wakeRead = fds[0];
  ex->wakeWrite = fds[1];
#endif
}

/* Safe from any thread. */
static void wake_signal(RuneExecutor *ex) {
#ifdef _WIN32
  char b = 1;
  send(ex->wake, &b, 1, 0);
#else
  char b = 1;
  ssize_t n = write(ex->wakeWrite, &b, 1);
  (void)n;   /* a full pipe already means a wake-up is pending */
#endif
}

static void wake_drain(RuneExecutor *ex) {
  char buf[64];
#ifdef _WIN32
  while (recv(ex->wake, buf, sizeof(buf), 0) > 0) {}
#else
  while (read(ex->wakeRead, buf, sizeof(buf)) > 0) {}
#endif
}

static void wake_close(RuneExecutor *ex) {
#ifdef _WIN32
  closesocket(ex->wake);
#else
  close(ex->wakeRead);
  close(ex->wakeWrite);
#endif
}

static RuneExecutor *executor(void) {
  if (g_executor)
    return g_executor;
  RuneExecutor *ex = (RuneExecutor *)calloc(1, sizeof(RuneExecutor));
  if (!ex)
    task_fatal("cannot allocate the task executor");
  ex->lock = rune_mutex_new();
  ex->cond = rune_cond_new();
  if (!ex->lock || !ex->cond)
    task_fatal("cannot create the task executor's lock");
  wake_open(ex);
#ifdef _WIN32
  ex->rootFiber = IsThreadAFiber() ? GetCurrentFiber() : ConvertThreadToFiber(NULL);
  if (!ex->rootFiber)
    task_fatal("cannot make this thread's fiber");
#else
  pthread_once(&g_executor_key_once, executor_key_init);
  pthread_setspecific(g_executor_key, ex);
#endif
  g_executor = ex;
  return ex;
}

#ifndef _WIN32
/* Runs when a thread that used tasks exits. Anything still queued belongs to
 * a task that never finished; its stack goes, and what the task held is
 * simply never released — the same fate as a thread's locals when the
 * process ends. */
static void executor_destroy(void *raw) {
  RuneExecutor *ex = (RuneExecutor *)raw;
  if (!ex)
    return;
  wake_close(ex);
  rune_mutex_dispose(ex->lock);
  rune_cond_dispose(ex->cond);
  free(ex->timers);
  free(ex->remote);
  free(ex->io);
  free(ex);
  for (size_t i = 0; i < g_stack_pool.count; ++i)
    munmap(g_stack_pool.mappings[i], g_stack_pool.sizes[i]);
  g_stack_pool.count = 0;
  g_executor = NULL;
}
#endif

/*--- The ready queue ---------------------------------------------------*/

static void ready_push(RuneExecutor *ex, RuneTask *t) {
  t->state = TASK_READY;
  t->next = NULL;
  if (ex->readyTail)
    ex->readyTail->next = t;
  else
    ex->readyHead = t;
  ex->readyTail = t;
}

static RuneTask *ready_pop(RuneExecutor *ex) {
  RuneTask *t = ex->readyHead;
  if (!t)
    return NULL;
  ex->readyHead = t->next;
  if (!ex->readyHead)
    ex->readyTail = NULL;
  t->next = NULL;
  return t;
}

static void ready_remove(RuneExecutor *ex, RuneTask *t) {
  RuneTask *prev = NULL;
  for (RuneTask *cur = ex->readyHead; cur; prev = cur, cur = cur->next) {
    if (cur != t)
      continue;
    if (prev)
      prev->next = cur->next;
    else
      ex->readyHead = cur->next;
    if (ex->readyTail == cur)
      ex->readyTail = prev;
    cur->next = NULL;
    return;
  }
}

/*--- Growable arrays of tasks -------------------------------------------*/

static void tasks_push(RuneTask ***arr, size_t *count, size_t *cap,
                       RuneTask *t, const char *what) {
  if (*count == *cap) {
    size_t grown = *cap ? *cap * 2 : 4;
    RuneTask **next = (RuneTask **)realloc(*arr, grown * sizeof(*next));
    if (!next)
      task_fatal(what);
    *arr = next;
    *cap = grown;
  }
  (*arr)[(*count)++] = t;
}

static void tasks_remove(RuneTask **arr, size_t *count, RuneTask *t) {
  for (size_t i = 0; i < *count; ++i) {
    if (arr[i] != t)
      continue;
    arr[i] = arr[*count - 1];
    (*count)--;
    return;
  }
}

/*--- Timers: a heap ordered by deadline --------------------------------*/

static void heap_swap(RuneExecutor *ex, size_t a, size_t b) {
  RuneTask *ta = ex->timers[a], *tb = ex->timers[b];
  ex->timers[a] = tb;
  ex->timers[b] = ta;
  tb->heapIndex = a;
  ta->heapIndex = b;
}

static void heap_up(RuneExecutor *ex, size_t i) {
  while (i > 0) {
    size_t parent = (i - 1) / 2;
    if (ex->timers[parent]->deadline <= ex->timers[i]->deadline)
      break;
    heap_swap(ex, parent, i);
    i = parent;
  }
}

static void heap_down(RuneExecutor *ex, size_t i) {
  for (;;) {
    size_t l = 2 * i + 1, r = l + 1, least = i;
    if (l < ex->timerCount &&
        ex->timers[l]->deadline < ex->timers[least]->deadline)
      least = l;
    if (r < ex->timerCount &&
        ex->timers[r]->deadline < ex->timers[least]->deadline)
      least = r;
    if (least == i)
      break;
    heap_swap(ex, i, least);
    i = least;
  }
}

static void timer_add(RuneExecutor *ex, RuneTask *t) {
  if (ex->timerCount == ex->timerCap) {
    size_t cap = ex->timerCap ? ex->timerCap * 2 : 8;
    RuneTask **grown = (RuneTask **)realloc(ex->timers, cap * sizeof(*grown));
    if (!grown)
      task_fatal("cannot grow the timer heap");
    ex->timers = grown;
    ex->timerCap = cap;
  }
  t->heapIndex = ex->timerCount;
  ex->timers[ex->timerCount++] = t;
  heap_up(ex, t->heapIndex);
}

static void timer_remove_at(RuneExecutor *ex, size_t i) {
  size_t last = ex->timerCount - 1;
  if (i != last)
    heap_swap(ex, i, last);
  ex->timerCount--;
  if (i < ex->timerCount) {
    heap_up(ex, i);
    heap_down(ex, i);
  }
}

static int timer_holds(RuneExecutor *ex, RuneTask *t) {
  return t->heapIndex < ex->timerCount && ex->timers[t->heapIndex] == t;
}

/*--- Waiting on one another ---------------------------------------------*/

/* Marks `t` finished and queues everyone parked on it. Always on the
 * executor's own thread: another thread's completion is posted through
 * `remote` and applied here. */
static void task_complete(RuneTask *t) {
  RuneExecutor *ex = t->exec;
  t->state = TASK_DONE;
  /* A task waiting on several of us at once is queued by the first to
   * finish; the rest find it already on the queue and leave it there. */
  for (size_t i = 0; i < t->waiterCount; ++i) {
    RuneTask *w = t->waiters[i];
    if (w->state == TASK_WAITING)
      ready_push(ex, w);
  }
  t->waiterCount = 0;
}

/* Parks `cur` on every task in `on`, remembering the set so a cancel — or
 * the first of them to finish — can take it off the rest. */
static void park_on(RuneTask *cur, RuneTask **on, int64_t count) {
  for (int64_t i = 0; i < count; ++i) {
    tasks_push(&on[i]->waiters, &on[i]->waiterCount, &on[i]->waiterCap, cur,
               "cannot grow a task's waiter list");
    tasks_push(&cur->waitingOn, &cur->waitingOnCount, &cur->waitingOnCap,
               on[i], "cannot record what a task waits on");
  }
  cur->state = TASK_WAITING;
}

/* The task is off the queue again, one way or another: forget the set. */
static void unpark(RuneTask *cur) {
  for (size_t i = 0; i < cur->waitingOnCount; ++i) {
    RuneTask *on = cur->waitingOn[i];
    tasks_remove(on->waiters, &on->waiterCount, cur);
  }
  cur->waitingOnCount = 0;
}

/*--- Switching -----------------------------------------------------------*/

static void fiber_finish(RuneTask *t);
#if !defined(_WIN32) && !RUNE_CTX_ASM
static void fiber_main(void);
#endif

#ifdef _WIN32

static void CALLBACK fiber_main(LPVOID raw) {
  RuneTask *t = (RuneTask *)raw;
  t->entry(t->payload);
  fiber_finish(t);
}

static void fiber_make(RuneTask *t) {
  t->fiber.handle = CreateFiber((SIZE_T)g_stack_size, fiber_main, t);
  if (!t->fiber.handle)
    task_fatal("cannot make a task's stack");
  t->fiberMade = 1;
}

static void fiber_release(RuneTask *t) {
  if (t->fiberMade && t->fiber.handle)
    DeleteFiber(t->fiber.handle);
  t->fiberMade = 0;
}

static void switch_to(RuneExecutor *ex, RuneTask *from, RuneTask *to) {
  (void)from;
  SwitchToFiber(to ? to->fiber.handle : ex->rootFiber);
}

#else

static void fiber_make(RuneTask *t) {
  size_t size = (size_t)g_stack_size + RUNE_STACK_GUARD;
  void *m = stack_map(size);
  if (!m)
    task_fatal("cannot map a task's stack");
  t->fiber.mapping = m;
  t->fiber.mappingSize = size;
#if RUNE_CTX_ASM
  fiber_layout(&t->fiber, (char *)m + size, t);
#else
  if (getcontext(&t->fiber.ctx) != 0)
    task_fatal("cannot capture a context for a task");
  t->fiber.ctx.uc_stack.ss_sp = (char *)m + RUNE_STACK_GUARD;
  t->fiber.ctx.uc_stack.ss_size = (size_t)g_stack_size;
  t->fiber.ctx.uc_link = NULL;
  makecontext(&t->fiber.ctx, fiber_main, 0);
#endif
  t->fiberMade = 1;
}

static void fiber_release(RuneTask *t) {
  if (t->fiberMade)
    stack_unmap(t->fiber.mapping, t->fiber.mappingSize);
  t->fiberMade = 0;
  t->fiber.mapping = NULL;
}

#if RUNE_CTX_ASM

/* The first thing a task's stack runs, reached from the trampoline. */
void rune_ctx_entry(void *raw) {
  RuneTask *t = (RuneTask *)raw;
  t->entry(t->payload);
  fiber_finish(t);
}

static void switch_to(RuneExecutor *ex, RuneTask *from, RuneTask *to) {
  void **save = from ? &from->fiber.sp : &ex->rootSp;
  void *load = to ? to->fiber.sp : ex->rootSp;
  rune_ctx_switch(save, load);
}

#else

static void fiber_main(void) {
  RuneTask *t = executor()->current;
  t->entry(t->payload);
  fiber_finish(t);
}

static void switch_to(RuneExecutor *ex, RuneTask *from, RuneTask *to) {
  ucontext_t *save = from ? &from->fiber.ctx : &ex->root;
  ucontext_t *load = to ? &to->fiber.ctx : &ex->root;
  swapcontext(save, load);
}

#endif
#endif

static void task_free_now(RuneTask *t);

/* Runs `t` until it parks, yields or finishes, then comes back here. The
 * caller may be the thread's own stack or another task; either way `t`
 * yields to exactly who resumed it, so a chain of resumes unwinds in order. */
static void task_resume(RuneExecutor *ex, RuneTask *t) {
  if (!t->fiberMade)
    fiber_make(t);
  RuneTask *prev = ex->current;
  t->resumer = prev;
  t->state = TASK_RUNNING;
  ex->current = t;
  switch_to(ex, prev, t);
  ex->current = prev;
  /* Back on this stack, which is not `t`'s: safe to take its stack away. */
  if (t->freeLater && t->state == TASK_DONE)
    task_free_now(t);
}

/* Hands the processor back to whoever resumed the current task. */
static void task_yield(RuneExecutor *ex) {
  RuneTask *t = ex->current;
  switch_to(ex, t, t->resumer);
}

/* The last thing a task's stack does. It never returns: the stack is torn
 * down by whoever frees the task, once nothing is running on it. */
static void fiber_finish(RuneTask *t) {
  RuneExecutor *ex = t->exec;
  task_complete(t);
  switch_to(ex, t, t->resumer);
  abort();
}

/*--- The loop -------------------------------------------------------------*/

/* Applies what other threads have finished since the last look. */
static void take_remote(RuneExecutor *ex) {
  if (ex->remoteCount == 0)
    return;
  rune_mutex_lock(ex->lock);
  size_t n = ex->remoteCount;
  RuneTask **done = (RuneTask **)malloc(n * sizeof(*done));
  if (!done)
    task_fatal("cannot collect finished tasks");
  memcpy(done, ex->remote, n * sizeof(*done));
  ex->remoteCount = 0;
  rune_mutex_unlock(ex->lock);
  for (size_t i = 0; i < n; ++i) {
    ex->externalPending--;
    if (done[i]->state != TASK_DONE)
      task_complete(done[i]);
  }
  free(done);
}

static void fire_timers(RuneExecutor *ex) {
  if (ex->timerCount == 0)
    return;
  int64_t now = rune_monotonic_ns();
  while (ex->timerCount > 0 && ex->timers[0]->deadline <= now) {
    RuneTask *t = ex->timers[0];
    timer_remove_at(ex, 0);
    task_complete(t);
  }
}

/* Looks at every socket being waited on, waiting up to `timeoutMs` for one
 * of them — or the wake-up pipe — to become ready. Sockets that are ready
 * complete their tasks. */
static void poll_io(RuneExecutor *ex, int timeoutMs) {
#ifdef _WIN32
  typedef WSAPOLLFD PollFd;
  const short kIn = POLLRDNORM, kOut = POLLWRNORM;
#else
  typedef struct pollfd PollFd;
  const short kIn = POLLIN, kOut = POLLOUT;
#endif
  size_t n = ex->ioCount + 1;
  PollFd *fds = (PollFd *)calloc(n, sizeof(PollFd));
  if (!fds)
    task_fatal("cannot allocate the poll set");
#ifdef _WIN32
  fds[0].fd = ex->wake;
#else
  fds[0].fd = ex->wakeRead;
#endif
  fds[0].events = kIn;
  for (size_t i = 0; i < ex->ioCount; ++i) {
    RuneTask *t = ex->io[i];
#ifdef _WIN32
    fds[i + 1].fd = (SOCKET)t->fd;
#else
    fds[i + 1].fd = (int)t->fd;
#endif
    fds[i + 1].events = (short)((t->ioEvents & 1 ? kIn : 0) |
                                (t->ioEvents & 2 ? kOut : 0));
  }
#ifdef _WIN32
  int r = WSAPoll(fds, (ULONG)n, timeoutMs);
#else
  int r = poll(fds, (nfds_t)n, timeoutMs);
#endif
  if (r > 0) {
    if (fds[0].revents)
      wake_drain(ex);
    /* Completing a task takes it out of `io`, so walk a copy of the set. */
    for (size_t i = 0; i < n - 1; ++i) {
      if (!fds[i + 1].revents)
        continue;
      RuneTask *t = NULL;
      for (size_t j = 0; j < ex->ioCount; ++j)
        if (ex->io[j]->fd == (int64_t)fds[i + 1].fd) {
          t = ex->io[j];
          break;
        }
      if (!t)
        continue;
      tasks_remove(ex->io, &ex->ioCount, t);
      task_complete(t);
    }
  }
  free(fds);
}

/* Waits for something to become ready when nothing is: the next timer, a
 * socket, or a completion from another thread. With none of those in
 * prospect the wait would never end, and saying so beats hanging. */
static void idle(RuneExecutor *ex) {
  rune_mutex_lock(ex->lock);
  size_t remote = ex->remoteCount;
  rune_mutex_unlock(ex->lock);
  if (remote > 0)
    return;
  int timeoutMs = -1;
  if (ex->timerCount > 0) {
    int64_t wait = ex->timers[0]->deadline - rune_monotonic_ns();
    if (wait <= 0)
      return;
    timeoutMs = (int)((wait + 999999) / 1000000);
    if (timeoutMs < 1)
      timeoutMs = 1;
  } else if (ex->externalPending == 0 && ex->ioCount == 0) {
    task_fatal("deadlock: waiting on a future that nothing will finish");
  }
  poll_io(ex, timeoutMs);
}

/* One turn of the loop. True when something ran. */
static int run_one(RuneExecutor *ex) {
  take_remote(ex);
  fire_timers(ex);
  if (ex->ioCount > 0)
    poll_io(ex, 0);
  RuneTask *t = ready_pop(ex);
  if (!t)
    return 0;
  task_resume(ex, t);
  return 1;
}

/*===--------------------------------------------------------------------===*\
|* The interface `std::task` calls
\*===--------------------------------------------------------------------===*/

static RuneTask *task_alloc(RuneExecutor *ex, int kind) {
  RuneTask *t = (RuneTask *)calloc(1, sizeof(RuneTask));
  if (!t)
    task_fatal("cannot allocate a task");
  t->exec = ex;
  t->kind = kind;
  t->state = TASK_NEW;
  return t;
}

static void require_here(RuneTask *t, RuneExecutor *ex) {
  if (t->exec != ex)
    task_fatal("a future belongs to the thread that made it");
}

/* A task that will run `entry(payload)` on a stack of its own. Nothing
 * happens until `rune_task_start`. */
void *rune_task_new(void (*entry)(void *), void *payload) {
  RuneTask *t = task_alloc(executor(), TASK_FIBER);
  t->entry = entry;
  t->payload = payload;
  return t;
}

/* Runs the task now, on this thread, until it first has to wait. */
void rune_task_start(void *raw) {
  RuneTask *t = (RuneTask *)raw;
  RuneExecutor *ex = executor();
  require_here(t, ex);
  if (t->state != TASK_NEW)
    return;
  task_resume(ex, t);
}

/* What a finished task's waiter is told. */
static int64_t finished_as(RuneTask *t) {
  return t->cancelledExit ? WAIT_TARGET_GONE : WAIT_DONE;
}

/* True when the current task is being cancelled at a point where it may
 * leave: inside a suspension point, with a cancel asked for. */
static int leaving(RuneTask *cur) {
  return cur && cur->cancelRequested && cur->interruptible > 0;
}

/* Blocks until `t` is done. Inside a task that means parking this one and
 * letting the rest run; on the thread's own stack it means running them
 * here, in this call, until `t` finishes. Comes back with WAIT_DONE, or
 * WAIT_CANCELLED when the waiting task was cancelled at a point where it may
 * leave, or WAIT_TARGET_GONE when `t` ended without a value. */
int64_t rune_task_wait(void *raw) {
  RuneTask *t = (RuneTask *)raw;
  RuneExecutor *ex = executor();
  require_here(t, ex);
  if (t->state == TASK_NEW)
    task_resume(ex, t);
  RuneTask *cur = ex->current;
  if (cur) {
    if (cur == t)
      task_fatal("a task cannot wait for itself");
    while (t->state != TASK_DONE) {
      if (leaving(cur))
        return WAIT_CANCELLED;
      park_on(cur, &t, 1);
      task_yield(ex);
      unpark(cur);
    }
    return finished_as(t);
  }
  while (t->state != TASK_DONE) {
    if (run_one(ex))
      continue;
    if (t->state == TASK_DONE)
      break;
    idle(ex);
  }
  return finished_as(t);
}

/* Blocks until any one of `count` tasks is done, and says which — the
 * lowest index among those that are. `raw` is an array of `count` task
 * pointers. Inside a task this parks it on every one of them; the first to
 * finish wakes it, and it takes itself off the others before returning.
 * Comes back with -1 when the waiting task was cancelled. */
int64_t rune_task_wait_any(void *raw, int64_t count) {
  RuneTask **tasks = (RuneTask **)raw;
  RuneExecutor *ex = executor();
  if (count <= 0)
    task_fatal("first: nothing to wait for");
  for (int64_t i = 0; i < count; ++i) {
    require_here(tasks[i], ex);
    if (tasks[i]->state == TASK_NEW)
      task_resume(ex, tasks[i]);
  }
  for (;;) {
    take_remote(ex);
    fire_timers(ex);
    if (ex->ioCount > 0)
      poll_io(ex, 0);
    for (int64_t i = 0; i < count; ++i)
      if (tasks[i]->state == TASK_DONE)
        return i;
    RuneTask *cur = ex->current;
    if (cur) {
      if (leaving(cur))
        return -1;
      for (int64_t i = 0; i < count; ++i)
        if (tasks[i] == cur)
          task_fatal("a task cannot wait for itself");
      park_on(cur, tasks, count);
      task_yield(ex);
      unpark(cur);
      continue;   /* something finished, or a cancel: the top of the loop */
    }
    if (run_one(ex))
      continue;
    idle(ex);
  }
}

int64_t rune_task_is_done(void *raw) {
  RuneTask *t = (RuneTask *)raw;
  if (t->kind != TASK_FIBER && t->state != TASK_DONE) {
    /* A timer, a socket or a remote completion may have landed since anyone
     * looked. */
    RuneExecutor *ex = t->exec;
    if (ex == executor()) {
      take_remote(ex);
      fire_timers(ex);
      if (ex->ioCount > 0)
        poll_io(ex, 0);
    }
  }
  return t->state == TASK_DONE ? 1 : 0;
}

/* Lets everything that is ready run before this continues. Inside a task the
 * task goes to the back of the queue; on the thread's own stack the queue is
 * drained once. */
void rune_task_yield_now(void) {
  RuneExecutor *ex = executor();
  RuneTask *cur = ex->current;
  if (cur) {
    ready_push(ex, cur);
    task_yield(ex);
    return;
  }
  take_remote(ex);
  fire_timers(ex);
  if (ex->ioCount > 0)
    poll_io(ex, 0);
  size_t n = 0;
  for (RuneTask *t = ex->readyHead; t; t = t->next)
    n++;
  while (n-- > 0) {
    RuneTask *t = ready_pop(ex);
    if (!t)
      break;
    task_resume(ex, t);
  }
}

/* A task that finishes by itself once `nanos` have passed. */
void *rune_task_timer(int64_t nanos) {
  RuneExecutor *ex = executor();
  RuneTask *t = task_alloc(ex, TASK_TIMER);
  t->deadline = rune_monotonic_ns() + (nanos > 0 ? nanos : 0);
  t->state = TASK_WAITING;
  timer_add(ex, t);
  return t;
}

/* A task that finishes when the socket `fd` is ready: `events` is 1 to read,
 * 2 to write, 3 for either. Errors and hang-ups count as ready, so the
 * read or write that follows is what reports them. */
void *rune_task_io(int64_t fd, int64_t events) {
  RuneExecutor *ex = executor();
  RuneTask *t = task_alloc(ex, TASK_IO);
  t->fd = fd;
  t->ioEvents = (int)(events ? events : 1);
  t->state = TASK_WAITING;
  tasks_push(&ex->io, &ex->ioCount, &ex->ioCap, t,
             "cannot grow the executor's socket list");
  return t;
}

/* A task another thread will finish with `rune_task_complete_remote`. */
void *rune_task_external(void) {
  RuneExecutor *ex = executor();
  RuneTask *t = task_alloc(ex, TASK_EXTERNAL);
  t->state = TASK_WAITING;
  ex->externalPending++;
  return t;
}

/* A task that code on this thread finishes with `rune_task_complete`. */
void *rune_task_manual(void) {
  RuneExecutor *ex = executor();
  RuneTask *t = task_alloc(ex, TASK_MANUAL);
  t->state = TASK_WAITING;
  return t;
}

/* Finishes a manual task, from the thread that made it. */
void rune_task_complete(void *raw) {
  RuneTask *t = (RuneTask *)raw;
  require_here(t, executor());
  if (t->state == TASK_DONE)
    return;
  task_complete(t);
}

/* Safe from any thread: posts the completion for the owning executor to
 * apply the next time it looks, and wakes it if it is idle. */
void rune_task_complete_remote(void *raw) {
  RuneTask *t = (RuneTask *)raw;
  RuneExecutor *ex = t->exec;
  rune_mutex_lock(ex->lock);
  if (ex->remoteCount == ex->remoteCap) {
    size_t cap = ex->remoteCap ? ex->remoteCap * 2 : 8;
    RuneTask **grown = (RuneTask **)realloc(ex->remote, cap * sizeof(*grown));
    if (!grown) {
      rune_mutex_unlock(ex->lock);
      task_fatal("cannot record a finished task");
    }
    ex->remote = grown;
    ex->remoteCap = cap;
  }
  ex->remote[ex->remoteCount++] = t;
  t->posted = 1;
  rune_cond_broadcast(ex->cond);
  rune_mutex_unlock(ex->lock);
  wake_signal(ex);
}

/* Blocks this thread until the other thread has posted `t`'s completion —
 * which is what makes it safe to free what that thread writes into. */
void rune_task_wait_posted(void *raw) {
  RuneTask *t = (RuneTask *)raw;
  RuneExecutor *ex = t->exec;
  if (t->kind != TASK_EXTERNAL)
    return;
  rune_mutex_lock(ex->lock);
  while (!t->posted)
    rune_cond_wait(ex->cond, ex->lock);
  rune_mutex_unlock(ex->lock);
}

/*--- Cancellation ---------------------------------------------------------*/

/* Asks `t` to stop. A task parked at a suspension point is woken and leaves
 * the point at once; one that is running leaves at its next. A timer, a
 * socket wait or a pending future simply ends, without a value. Tasks the
 * cancelled one started are not touched. */
void rune_task_cancel(void *raw) {
  RuneTask *t = (RuneTask *)raw;
  RuneExecutor *ex = executor();
  require_here(t, ex);
  if (t->state == TASK_DONE || t->cancelRequested)
    return;
  t->cancelRequested = 1;
  switch (t->kind) {
  case TASK_FIBER:
    if (t->state == TASK_WAITING && t->interruptible > 0) {
      unpark(t);
      ready_push(ex, t);
    }
    return;
  case TASK_TIMER:
    if (timer_holds(ex, t))
      timer_remove_at(ex, t->heapIndex);
    break;
  case TASK_IO:
    tasks_remove(ex->io, &ex->ioCount, t);
    break;
  case TASK_MANUAL:
  case TASK_EXTERNAL:
    /* An external task's thread runs on; its result is simply not wanted. */
    break;
  }
  t->cancelledExit = 1;
  task_complete(t);
}

/* Whether a cancel has been asked for. */
int64_t rune_task_is_cancelled(void *raw) {
  return ((RuneTask *)raw)->cancelRequested ? 1 : 0;
}

/* Whether the task ended without a value because it was cancelled. */
int64_t rune_task_exited_cancelled(void *raw) {
  return ((RuneTask *)raw)->cancelledExit ? 1 : 0;
}

/* The compiler brackets every suspension point in an `async` body with
 * these. Between them a wait may be cut short by a cancel; `leave` says
 * whether the task is leaving because of one, and records that it did. */
void rune_task_suspend_enter(void) {
  RuneTask *cur = g_executor ? g_executor->current : NULL;
  if (cur)
    cur->interruptible++;
}

int64_t rune_task_suspend_leave(void) {
  RuneTask *cur = g_executor ? g_executor->current : NULL;
  if (!cur)
    return 0;
  if (cur->interruptible > 0)
    cur->interruptible--;
  if (cur->cancelRequested) {
    cur->cancelledExit = 1;
    return 1;
  }
  return 0;
}

/*--- Freeing ----------------------------------------------------------------*/

/* Frees a task nothing refers to any more. A task still on a stack cannot be
 * freed — its own body holds the future that owns it — so by the time this
 * is called the task is new, done, or a timer, socket, manual or external
 * task nobody is waiting for. */
void rune_task_free(void *raw) {
  RuneTask *t = (RuneTask *)raw;
  if (!t)
    return;
  RuneExecutor *ex = t->exec;
  if (ex->current == t) {
    /* Freed from its own stack: the body let go of the last reference to
     * its future. The stack is still in use, so it goes when the task does. */
    t->freeLater = 1;
    return;
  }
  task_free_now(t);
}

static void task_free_now(RuneTask *t) {
  RuneExecutor *ex = t->exec;
  if (t->kind == TASK_TIMER && timer_holds(ex, t))
    timer_remove_at(ex, t->heapIndex);
  if (t->kind == TASK_IO)
    tasks_remove(ex->io, &ex->ioCount, t);
  if (t->kind == TASK_EXTERNAL) {
    /* The other thread posts once, whatever happened here; that post is
     * either already applied, or waiting in `remote`. */
    rune_task_wait_posted(t);
    rune_mutex_lock(ex->lock);
    size_t before = ex->remoteCount;
    tasks_remove(ex->remote, &ex->remoteCount, t);
    int wasPending = ex->remoteCount != before;
    rune_mutex_unlock(ex->lock);
    if (wasPending)
      ex->externalPending--;
  }
  if (t->state == TASK_READY)
    ready_remove(ex, t);
  if (t->state == TASK_WAITING)
    unpark(t);
  fiber_release(t);
  free(t->waiters);
  free(t->waitingOn);
  free(t);
}

/* True inside a task; false on a thread's own stack. */
int64_t rune_task_in_task(void) {
  return g_executor && g_executor->current ? 1 : 0;
}

int64_t rune_task_stack_size(void) { return g_stack_size; }

/* Applies to tasks started from here on; a running task keeps its stack. */
void rune_task_set_stack_size(int64_t bytes) {
  if (bytes < 16384)
    bytes = 16384;
  /* Whole pages, so the guard page sits exactly below the stack. */
  bytes = (bytes + 4095) & ~(int64_t)4095;
  g_stack_size = bytes;
}

/*===--------------------------------------------------------------------===*\
|* The worker pool
|*
|* Threads for work that would otherwise hold up an executor. There are at
|* most as many as the machine runs at once; each is started the first time
|* it is needed and then waits for the next job. A worker is an ordinary
|* thread, so it has an executor of its own, and a job may start tasks there
|* and wait for them.
\*===--------------------------------------------------------------------===*/

typedef struct RuneJob {
  void *(*entry)(void *);
  void *argument;
  struct RuneJob *next;
} RuneJob;

static void *g_pool_lock;
static void *g_pool_cond;
static RuneJob *g_pool_head, *g_pool_tail;
static int64_t g_pool_workers, g_pool_idle, g_pool_limit;

#ifdef _WIN32
static INIT_ONCE g_pool_once = INIT_ONCE_STATIC_INIT;
static BOOL CALLBACK pool_init(PINIT_ONCE o, PVOID p, PVOID *c) {
  (void)o; (void)p; (void)c;
  g_pool_lock = rune_mutex_new();
  g_pool_cond = rune_cond_new();
  return TRUE;
}
static void pool_ensure(void) {
  InitOnceExecuteOnce(&g_pool_once, pool_init, NULL, NULL);
}
#else
static pthread_once_t g_pool_once = PTHREAD_ONCE_INIT;
static void pool_init(void) {
  g_pool_lock = rune_mutex_new();
  g_pool_cond = rune_cond_new();
}
static void pool_ensure(void) { pthread_once(&g_pool_once, pool_init); }
#endif

#ifdef _WIN32
static DWORD WINAPI pool_worker_win(LPVOID unused);
#endif

static void *pool_worker(void *unused) {
  (void)unused;
  for (;;) {
    rune_mutex_lock(g_pool_lock);
    while (!g_pool_head) {
      g_pool_idle++;
      rune_cond_wait(g_pool_cond, g_pool_lock);
      g_pool_idle--;
    }
    RuneJob *job = g_pool_head;
    g_pool_head = job->next;
    if (!g_pool_head)
      g_pool_tail = NULL;
    rune_mutex_unlock(g_pool_lock);
    job->entry(job->argument);
    free(job);
  }
  return NULL;
}

/* Runs `entry(argument)` on a worker thread, starting one if every worker
 * is busy and the pool is not yet full. */
void rune_pool_submit(void *(*entry)(void *), void *argument) {
  pool_ensure();
  RuneJob *job = (RuneJob *)calloc(1, sizeof(RuneJob));
  if (!job)
    task_fatal("cannot allocate a job for the worker pool");
  job->entry = entry;
  job->argument = argument;
  rune_mutex_lock(g_pool_lock);
  if (g_pool_tail)
    g_pool_tail->next = job;
  else
    g_pool_head = job;
  g_pool_tail = job;
  if (g_pool_limit == 0) {
    g_pool_limit = rune_hardware_threads();
    if (g_pool_limit < 2)
      g_pool_limit = 2;
  }
  if (g_pool_idle == 0 && g_pool_workers < g_pool_limit) {
    /* Detached from the start: a worker lives as long as the process. */
#ifdef _WIN32
    HANDLE h = CreateThread(NULL, 0, pool_worker_win, NULL, 0, NULL);
    if (h) {
      CloseHandle(h);
      g_pool_workers++;
    }
#else
    pthread_t id;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&id, &attr, pool_worker, NULL) == 0)
      g_pool_workers++;
    pthread_attr_destroy(&attr);
#endif
  }
  rune_cond_signal(g_pool_cond);
  rune_mutex_unlock(g_pool_lock);
}

#ifdef _WIN32
static DWORD WINAPI pool_worker_win(LPVOID unused) {
  pool_worker(unused);
  return 0;
}
#endif

/* How many worker threads the pool may run at once. */
int64_t rune_pool_size(void) {
  pool_ensure();
  rune_mutex_lock(g_pool_lock);
  if (g_pool_limit == 0) {
    g_pool_limit = rune_hardware_threads();
    if (g_pool_limit < 2)
      g_pool_limit = 2;
  }
  int64_t n = g_pool_limit;
  rune_mutex_unlock(g_pool_lock);
  return n;
}
