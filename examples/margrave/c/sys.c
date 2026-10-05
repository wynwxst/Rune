// The few things margrave needs from the operating system that the standard
// library does not offer: a raw terminal, waiting on several descriptors at
// once, child processes on pipes, and POSIX regular expressions.
//
// Everything here is a thin wrapper. Policy — what a key means, when to
// redraw, what a command's output looks like — lives on the Rune side.

#define _DARWIN_C_SOURCE
#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 700

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <poll.h>
#include <regex.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <wchar.h>

//=== The terminal ========================================================//

static struct termios saved_mode;
static int raw_enabled = 0;
static int winch_pipe[2] = {-1, -1};

static void on_winch(int sig) {
    (void)sig;
    int saved = errno;
    if (winch_pipe[1] >= 0) {
        char b = 1;
        ssize_t r = write(winch_pipe[1], &b, 1);
        (void)r;
    }
    errno = saved;
}

static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void set_cloexec(int fd) {
    int flags = fcntl(fd, F_GETFD, 0);
    if (flags >= 0) fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

/// Puts the terminal in raw mode. 0 on success, -1 when standard input is not
/// a terminal at all.
int mg_term_enter(void) {
    setlocale(LC_CTYPE, "");
    if (!isatty(0)) return -1;
    if (tcgetattr(0, &saved_mode) != 0) return -1;
    struct termios raw = saved_mode;
    raw.c_iflag &= ~(tcflag_t)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(tcflag_t)(OPOST);
    raw.c_cflag |= (tcflag_t)(CS8);
    raw.c_lflag &= ~(tcflag_t)(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(0, TCSAFLUSH, &raw) != 0) return -1;
    raw_enabled = 1;

    if (winch_pipe[0] < 0 && pipe(winch_pipe) == 0) {
        set_nonblocking(winch_pipe[0]);
        set_nonblocking(winch_pipe[1]);
        set_cloexec(winch_pipe[0]);
        set_cloexec(winch_pipe[1]);
        struct sigaction sa;
        memset(&sa, 0, sizeof sa);
        sa.sa_handler = on_winch;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGWINCH, &sa, NULL);
    }
    // A child that dies must not take us with it, and writing to a language
    // server that has gone away must be an error rather than a signal.
    signal(SIGPIPE, SIG_IGN);
    return 0;
}

void mg_term_leave(void) {
    if (raw_enabled) {
        tcsetattr(0, TCSAFLUSH, &saved_mode);
        raw_enabled = 0;
    }
}

/// The window's size. Falls back to 80x24 when the terminal will not say.
void mg_term_size(int32_t *rows, int32_t *cols) {
    struct winsize ws;
    if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0 && ws.ws_col > 0) {
        *rows = ws.ws_row;
        *cols = ws.ws_col;
    } else {
        *rows = 24;
        *cols = 80;
    }
}

/// A descriptor that becomes readable when the window is resized.
int32_t mg_winch_fd(void) { return winch_pipe[0]; }

/// The columns `cp` takes up on screen: 0, 1 or 2.
int32_t mg_wcwidth(uint32_t cp) {
    if (cp < 0x20) return 0;
    if (cp < 0x7f) return 1;
    int w = wcwidth((wchar_t)cp);
    if (w < 0) return 1;
    return w;
}

//=== Descriptors =========================================================//

/// Waits until one of `fds` is readable or `timeout` milliseconds pass (-1:
/// forever). `ready[i]` is set to 1 for each readable (or hung up) one.
/// Returns how many are ready.
int32_t mg_poll(const int32_t *fds, int32_t count, int32_t timeout, int32_t *ready) {
    struct pollfd p[16];
    if (count > 16) count = 16;
    for (int i = 0; i < count; i++) {
        p[i].fd = fds[i];
        p[i].events = POLLIN;
        p[i].revents = 0;
        ready[i] = 0;
    }
    int n;
    do {
        n = poll(p, (nfds_t)count, timeout);
    } while (n < 0 && errno == EINTR);
    if (n <= 0) return 0;
    for (int i = 0; i < count; i++) {
        if (p[i].revents & (POLLIN | POLLHUP | POLLERR)) ready[i] = 1;
    }
    return n;
}

/// Reads what is there, up to `most` bytes. >0 bytes read, 0 at the end of
/// the stream, -1 when nothing is waiting, -2 on an error.
int64_t mg_read(int32_t fd, uint8_t *into, int64_t most) {
    ssize_t n;
    do {
        n = read(fd, into, (size_t)most);
    } while (n < 0 && errno == EINTR);
    if (n < 0) return (errno == EAGAIN || errno == EWOULDBLOCK) ? -1 : -2;
    return (int64_t)n;
}

/// Writes all of it, waiting when the other side is slow. Returns what was
/// written, which is less than `count` only on an error.
int64_t mg_write(int32_t fd, const uint8_t *data, int64_t count) {
    int64_t done = 0;
    while (done < count) {
        ssize_t n = write(fd, data + done, (size_t)(count - done));
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                struct pollfd p = {fd, POLLOUT, 0};
                poll(&p, 1, 100);
                continue;
            }
            break;
        }
        done += n;
    }
    return done;
}

void mg_close(int32_t fd) {
    if (fd >= 0) close(fd);
}

//=== Children ============================================================//

/// Starts `/bin/sh -c command` in `directory` (empty: here).
///
/// `*output` reads what it writes; with `merge` its errors go there too,
/// otherwise they are thrown away. With `input` set, `*input` writes to its
/// standard input; otherwise that is /dev/null. The child leads a process
/// group of its own, so stopping it stops whatever it started.
///
/// Returns the pid, or -1 when it could not be started.
int32_t mg_spawn(const char *command, const char *directory, int32_t input,
                 int32_t merge, int32_t *input_fd, int32_t *output_fd) {
    int out[2], in[2] = {-1, -1};
    if (pipe(out) != 0) return -1;
    if (input && pipe(in) != 0) {
        close(out[0]);
        close(out[1]);
        return -1;
    }
    pid_t pid = fork();
    if (pid < 0) {
        close(out[0]); close(out[1]);
        if (input) { close(in[0]); close(in[1]); }
        return -1;
    }
    if (pid == 0) {
        setpgid(0, 0);
        signal(SIGPIPE, SIG_DFL);
        signal(SIGINT, SIG_DFL);
        signal(SIGWINCH, SIG_DFL);
        if (directory && directory[0]) {
            if (chdir(directory) != 0) _exit(127);
        }
        int devnull = open("/dev/null", O_RDWR);
        if (input) {
            dup2(in[0], 0);
        } else {
            dup2(devnull, 0);
        }
        dup2(out[1], 1);
        dup2(merge ? out[1] : devnull, 2);
        close(out[0]);
        close(out[1]);
        if (input) { close(in[0]); close(in[1]); }
        if (devnull > 2) close(devnull);
        execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        _exit(127);
    }
    setpgid(pid, pid);
    close(out[1]);
    set_nonblocking(out[0]);
    set_cloexec(out[0]);
    *output_fd = out[0];
    if (input) {
        close(in[0]);
        set_cloexec(in[1]);
        *input_fd = in[1];
    } else {
        *input_fd = -1;
    }
    return pid;
}

/// Asks whether `pid` has finished, without waiting. 1 when it has, with
/// `*status` its exit code (128 + n for signal n); 0 when it is still running.
int32_t mg_reap(int32_t pid, int32_t *status) {
    int st = 0;
    pid_t r = waitpid(pid, &st, WNOHANG);
    if (r == 0) return 0;
    if (r < 0) { *status = -1; return 1; }
    if (WIFEXITED(st)) *status = WEXITSTATUS(st);
    else if (WIFSIGNALED(st)) *status = 128 + WTERMSIG(st);
    else *status = -1;
    return 1;
}

/// Sends `signal` to the child's whole process group.
void mg_kill(int32_t pid, int32_t sig) {
    if (pid > 0) {
        if (kill(-pid, sig) != 0) kill(pid, sig);
    }
}

/// Runs `command` with `data` on its standard input and waits for it — what
/// putting text on a clipboard takes. Returns its exit status, -1 on failure.
int32_t mg_pipe_into(const char *command, const uint8_t *data, int64_t count) {
    int32_t in = -1, out = -1;
    int32_t pid = mg_spawn(command, "", 1, 0, &in, &out);
    if (pid < 0) return -1;
    mg_write(in, data, count);
    close(in);
    char sink[256];
    for (;;) {
        ssize_t n = read(out, sink, sizeof sink);
        if (n > 0) continue;
        if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
            struct pollfd p = {out, POLLIN, 0};
            poll(&p, 1, 200);
            continue;
        }
        break;
    }
    close(out);
    int st = 0;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

//=== Files ===============================================================//

/// 0: nothing there, 1: a file, 2: a directory, 3: an executable file,
/// 4: something else (a socket, a device).
int32_t mg_file_kind(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    if (S_ISDIR(st.st_mode)) return 2;
    if (S_ISREG(st.st_mode)) return (st.st_mode & 0111) ? 3 : 1;
    return 4;
}

int64_t mg_file_size(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (int64_t)st.st_size;
}

/// Seconds since the epoch the file was last written, or -1.
int64_t mg_file_mtime(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (int64_t)st.st_mtime;
}

//=== What a file is, by its first bytes ==================================//

static uint32_t mg_u32(const unsigned char *p, int big) {
    return big ? ((uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3])
               : ((uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0]);
}

static char kinds[256];
static char headHex[2 * 64 + 1];

static void mg_kind(const char *k) {
    size_t n = strlen(kinds);
    if (n + strlen(k) + 2 >= sizeof kinds) return;
    if (n) kinds[n++] = ' ';
    strcpy(kinds + n, k);
}

/// The kinds a file's first bytes say it is, space-separated: `macho
/// macho-exec`, `elf elf-exec`, `script`, `pdf`... Empty when nothing is
/// recognised or the file cannot be read. `executable` is added when its
/// permission bits say it may be run.
const char *mg_file_magic(const char *path) {
    kinds[0] = 0;
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) return kinds;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return kinds;
    unsigned char b[64];
    ssize_t n = read(fd, b, sizeof b);
    close(fd);
    if (n < 0) n = 0;
    int exec = (st.st_mode & 0111) != 0;

    if (n >= 16) {
        uint32_t be = mg_u32(b, 1);
        // Mach-O: feedface/feedfacf, either byte order; the file type at
        // offset 12 says whether it is a program, a library or an object.
        if (be == 0xfeedface || be == 0xfeedfacf || be == 0xcefaedfe || be == 0xcffaedfe) {
            int big = be == 0xfeedface || be == 0xfeedfacf;
            uint32_t type = mg_u32(b + 12, big);
            mg_kind("macho");
            if (type == 2) mg_kind("macho-exec");
            else if (type == 6) mg_kind("macho-dylib");
            else if (type == 1) mg_kind("macho-object");
        } else if (be == 0xcafebabe || be == 0xbebafeca) {
            // A universal binary — or a Java class, which has the same magic
            // followed by a version far larger than any architecture count.
            uint32_t count = mg_u32(b + 4, be == 0xcafebabe);
            if (count > 0 && count < 30) {
                mg_kind("macho");
                mg_kind("macho-universal");
                if (exec) mg_kind("macho-exec");
            } else {
                mg_kind("java-class");
            }
        }
    }
    if (n >= 18 && b[0] == 0x7f && b[1] == 'E' && b[2] == 'L' && b[3] == 'F') {
        int big = b[5] == 2;
        unsigned type = big ? (unsigned)(b[16] << 8 | b[17]) : (unsigned)(b[17] << 8 | b[16]);
        mg_kind("elf");
        // ET_EXEC, or ET_DYN with the bits to run it: a PIE program rather
        // than a shared library.
        if (type == 2 || (type == 3 && exec)) mg_kind("elf-exec");
        else if (type == 3) mg_kind("elf-shared");
        else if (type == 1) mg_kind("elf-object");
    }
    if (n >= 2 && b[0] == 'M' && b[1] == 'Z') mg_kind("pe");
    if (n >= 2 && b[0] == '#' && b[1] == '!') mg_kind("script");
    if (n >= 4 && memcmp(b, "\0asm", 4) == 0) mg_kind("wasm");
    if (n >= 5 && memcmp(b, "%PDF-", 5) == 0) mg_kind("pdf");
    if (n >= 8 && memcmp(b, "\x89PNG\r\n\x1a\n", 8) == 0) mg_kind("png");
    if (n >= 3 && b[0] == 0xff && b[1] == 0xd8 && b[2] == 0xff) mg_kind("jpeg");
    if (n >= 6 && (memcmp(b, "GIF87a", 6) == 0 || memcmp(b, "GIF89a", 6) == 0)) mg_kind("gif");
    if (n >= 4 && memcmp(b, "PK\x03\x04", 4) == 0) mg_kind("zip");
    if (n >= 2 && b[0] == 0x1f && b[1] == 0x8b) mg_kind("gzip");
    if (n >= 6 && memcmp(b, "\xfd" "7zXZ", 5) == 0) mg_kind("xz");
    if (n >= 3 && memcmp(b, "BZh", 3) == 0) mg_kind("bzip2");
    if (exec) mg_kind("executable");
    return kinds;
}

/// The first `count` bytes of a file, as lowercase hex — what a `0x...`
/// magic in actions.toml is compared against. Empty when unreadable.
const char *mg_file_head_hex(const char *path, int32_t count) {
    headHex[0] = 0;
    if (count <= 0) return headHex;
    if (count > 64) count = 64;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return headHex;
    unsigned char b[64];
    ssize_t n = read(fd, b, (size_t)count);
    close(fd);
    static const char digits[] = "0123456789abcdef";
    for (ssize_t i = 0; i < n; ++i) {
        headHex[2 * i] = digits[b[i] >> 4];
        headHex[2 * i + 1] = digits[b[i] & 15];
    }
    headHex[n > 0 ? 2 * n : 0] = 0;
    return headHex;
}

static char resolved[PATH_MAX + 1];

/// The canonical absolute path, or the empty string when it does not exist.
const char *mg_realpath(const char *path) {
    if (!realpath(path, resolved)) resolved[0] = 0;
    return resolved;
}

//=== Regular expressions =================================================//

/// A compiled POSIX extended expression, or NULL with the reason in `error`.
void *mg_regex_new(const char *pattern, int32_t icase, char *error, int32_t error_size) {
    regex_t *re = malloc(sizeof *re);
    if (!re) return NULL;
    int flags = REG_EXTENDED | REG_NEWLINE;
    if (icase) flags |= REG_ICASE;
    int rc = regcomp(re, pattern, flags);
    if (rc != 0) {
        if (error && error_size > 0) regerror(rc, re, error, (size_t)error_size);
        free(re);
        return NULL;
    }
    return re;
}

/// The first match in `text` at or after byte `from`. 1 with the match's byte
/// range in `*start`/`*end`; 0 when there is none.
int32_t mg_regex_find(void *handle, const char *text, int64_t from, int64_t *start, int64_t *end) {
    regex_t *re = handle;
    regmatch_t m[1];
    int flags = from > 0 ? REG_NOTBOL : 0;
    if (regexec(re, text + from, 1, m, flags) != 0) return 0;
    *start = from + (int64_t)m[0].rm_so;
    *end = from + (int64_t)m[0].rm_eo;
    return 1;
}

void mg_regex_free(void *handle) {
    if (handle) {
        regfree((regex_t *)handle);
        free(handle);
    }
}
