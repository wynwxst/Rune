//===- Diagnostics.h - Rich, boxed compiler diagnostics --------*- C++ -*-===//
//
// Rune renders diagnostics as connected boxes: a primary entry with a source
// snippet and caret, optional trailing notes, and any number of *related*
// entries (a second location that explains the first) joined to the primary by
// a vertical rule:
//
//   ┌ ● demo.rune [12:8..13]
//   │  11 ║     let total = 0
//   │  12 ║     total += price(item)
//   │            ^^^^^ ERROR: cannot assign to immutable binding 'total'
//   │     ─  note: declare it with `var` to allow mutation
//   │
//   │ ○ demo.rune [11:8..13]
//   └▶ 'total' bound here
//      11 ║     let total = 0
//               ─────────────> hint: this binding is immutable
//
//===----------------------------------------------------------------------===//
#ifndef RUNE_DIAGNOSTICS_H
#define RUNE_DIAGNOSTICS_H

#include "rune/Source.h"

#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <string_view>
#include <type_traits>
#include <utility>

namespace rune {

enum class Severity {
  Note,     ///< Informational; never fails the build.
  Remark,   ///< Optimisation / style remark.
  Warning,  ///< Suspicious but compilable.
  Error,    ///< Compilation cannot produce output.
  Fatal,    ///< Compilation stops immediately.
};

/// A stable identifier so users can `#[allow]` or grep for a specific message.
/// Codes are of the form E0001 / W0001; 0 means "uncoded".
struct DiagCode {
  unsigned Value = 0;
  bool isWarning = false;
  std::string str() const;
  explicit operator bool() const { return Value != 0; }
};

/// A secondary location attached to a diagnostic.
struct RelatedInfo {
  SourceRange Range;
  std::string Label; ///< Headline printed next to the `└▶` connector.
  std::string Hint;  ///< Printed after the `────>` arrow under the snippet.
  Severity Sev = Severity::Note;
};

struct Diagnostic {
  Severity Sev = Severity::Error;
  DiagCode Code;
  std::string Message;
  SourceRange Range;
  std::vector<std::string> Notes;
  std::vector<RelatedInfo> Related;
};

//===----------------------------------------------------------------------===//
// Tiny `{}` formatter
//===----------------------------------------------------------------------===//

namespace detail {
/// One argument, appended as text. Strings and integers are appended
/// directly; anything else goes through a stream, which is what decides how
/// a `double` or a pointer reads.
inline void appendArg(std::string &out, const std::string &v) { out += v; }
inline void appendArg(std::string &out, std::string_view v) { out += v; }
inline void appendArg(std::string &out, const char *v) {
  if (v)
    out += v;
}
inline void appendArg(std::string &out, char *v) {
  if (v)
    out += v;
}
inline void appendArg(std::string &out, char v) { out += v; }
template <typename T> void appendArg(std::string &out, const T &v) {
  if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool> &&
                !std::is_same_v<T, signed char> &&
                !std::is_same_v<T, unsigned char>) {
    out += std::to_string(v);
  } else {
    std::ostringstream os;
    os << v;
    out += os.str();
  }
}

/// The pattern from `p` up to its next `{}`, with `{{` and `}}` read as the
/// braces they stand for. Returns where the `{}` is, or null at the end.
inline const char *appendLiteral(std::string &out, const char *p) {
  const char *run = p;
  for (; *p; ++p) {
    if (p[0] == '{' && p[1] == '}') {
      out.append(run, p);
      return p;
    }
    if ((p[0] == '{' && p[1] == '{') || (p[0] == '}' && p[1] == '}')) {
      out.append(run, p + 1);
      ++p;
      run = p + 1;
    }
  }
  out.append(run, p);
  return nullptr;
}

/// No arguments left, so every remaining brace is a literal one. `{{` and
/// `}}` still stand for `{` and `}`: a message means the same thing whether or
/// not it happens to have run out of arguments before reaching them.
inline void formatInto(std::string &out, const char *pattern) {
  for (const char *p = pattern; p;) {
    p = appendLiteral(out, p);
    if (p) {
      out += "{}";
      p += 2;
    }
  }
}

template <typename T, typename... Rest>
void formatInto(std::string &out, const char *pattern, const T &arg,
                const Rest &...rest) {
  const char *p = appendLiteral(out, pattern);
  if (!p)
    return;
  appendArg(out, arg);
  formatInto(out, p + 2, rest...);
}
} // namespace detail

/// `fmt("expected {} — got {}", a, b)`
///
/// Named `fmt` rather than `format` so that argument-dependent lookup on a
/// `std::string` argument cannot drag in `std::format` and make the call
/// ambiguous.
template <typename... Args>
std::string fmt(const char *pattern, const Args &...args) {
  std::string out;
  out.reserve(64);
  detail::formatInto(out, pattern, args...);
  return out;
}

class DiagnosticEngine;

/// Fluent builder; the diagnostic is emitted when the builder dies.
class DiagBuilder {
public:
  DiagBuilder(DiagnosticEngine *engine, Diagnostic d)
      : Engine(engine), D(std::move(d)) {}
  DiagBuilder(DiagBuilder &&o) noexcept : Engine(o.Engine), D(std::move(o.D)) {
    o.Engine = nullptr;
  }
  DiagBuilder(const DiagBuilder &) = delete;
  DiagBuilder &operator=(const DiagBuilder &) = delete;
  ~DiagBuilder();

  template <typename... Args>
  DiagBuilder &note(const char *pattern, const Args &...args) {
    D.Notes.push_back(fmt(pattern, args...));
    return *this;
  }

  /// Attach a second location. `label` sits on the `└▶` line, `hint` follows
  /// the arrow beneath the snippet.
  DiagBuilder &related(SourceRange r, std::string label, std::string hint = {},
                       Severity sev = Severity::Note) {
    D.Related.push_back({r, std::move(label), std::move(hint), sev});
    return *this;
  }

  DiagBuilder &code(unsigned value) {
    D.Code = DiagCode{value, D.Sev == Severity::Warning};
    return *this;
  }

  /// Overrides the primary span without changing anything else.
  DiagBuilder &at(SourceRange r) {
    D.Range = r;
    return *this;
  }

private:
  DiagnosticEngine *Engine;
  Diagnostic D;
};

class DiagnosticEngine {
public:
  explicit DiagnosticEngine(const SourceManager &sm) : SM(sm) {}

  void setColorEnabled(bool on) { Color = on; }
  /// Auto-detects a TTY on stderr.
  void detectColor();
  void setWarningsAsErrors(bool on) { WarnAsError = on; }
  void setErrorLimit(unsigned n) { ErrorLimit = n; }
  /// Suppresses every warning; errors still print.
  void setQuietWarnings(bool on) { QuietWarnings = on; }
  /// One JSON object per line instead of the boxed rendering, for editors
  /// and other tools. Status lines are not written at all in this mode, so
  /// every line on stderr is a diagnostic.
  void setJsonOutput(bool on) { Json = on; }
  /// One `file:line:column: severity: message [code]` line per diagnostic —
  /// the compilers' old shape, which `make`-style tools and editors such as
  /// Vim already read. Notes and related places follow as `note:` lines.
  void setShortOutput(bool on) { Short = on; }
  /// Counts errors and warnings but writes nothing, and ignores every
  /// thread-wide setting — speculation, capture. For a throwaway engine that
  /// only has to answer "did that go wrong?": `beginSpeculation` would not
  /// do, since its depth belongs to the thread, not to this engine, and it
  /// discards errors without counting them.
  void setSilent(bool on) { Silent = on; }

  /// Keeps everything this engine would write — diagnostics in whatever
  /// format, status lines — until `release`, which writes it out in the
  /// order it came. Counting goes on as usual. A compile that may be redone
  /// another way holds its output, so that a first attempt that is thrown
  /// away leaves nothing behind.
  void hold() {
    std::lock_guard<std::mutex> lock(Mutex);
    Holding = true;
  }
  /// Writes what was held, and stops holding.
  void release();

  template <typename... Args>
  DiagBuilder error(SourceRange r, const char *pattern, const Args &...args) {
    return make(Severity::Error, r, fmt(pattern, args...));
  }
  template <typename... Args>
  DiagBuilder warn(SourceRange r, const char *pattern, const Args &...args) {
    return make(Severity::Warning, r, fmt(pattern, args...));
  }
  template <typename... Args>
  DiagBuilder note(SourceRange r, const char *pattern, const Args &...args) {
    return make(Severity::Note, r, fmt(pattern, args...));
  }
  template <typename... Args>
  DiagBuilder remark(SourceRange r, const char *pattern, const Args &...args) {
    return make(Severity::Remark, r, fmt(pattern, args...));
  }
  /// A diagnostic with no source location (driver-level failures).
  template <typename... Args>
  DiagBuilder fatal(const char *pattern, const Args &...args) {
    return make(Severity::Fatal, SourceRange(), fmt(pattern, args...));
  }

  void emit(const Diagnostic &d);

  /// Collects this thread's diagnostics into `into` instead of printing them,
  /// until `endCapture`.
  ///
  /// A pass that runs its items on several threads still has to report what
  /// it found in one settled order — the order of the items, not of whichever
  /// thread finished first. Each item reports into a bucket of its own, and
  /// the buckets are replayed in order afterwards, so the output does not
  /// depend on how the work was divided.
  void beginCapture(std::vector<Diagnostic> *into) { captureSink() = into; }
  void endCapture() { captureSink() = nullptr; }

  /// Emits `ds` as if they had been reported here and now.
  void replay(const std::vector<Diagnostic> &ds) {
    for (const Diagnostic &d : ds)
      emit(d);
  }

  /// While speculating, diagnostics are discarded and do not count as errors.
  /// The parser uses this to try an ambiguous production and back out cleanly.
  ///
  /// Per thread, because files are parsed together: one parser backing out of
  /// an ambiguous production must not swallow another's real error.
  void beginSpeculation() { ++suppressDepth(); }
  void endSpeculation() { if (suppressDepth()) --suppressDepth(); }
  bool speculating() const { return suppressDepth() > 0; }

  bool hadError() const { return ErrorCount > 0; }
  unsigned errorCount() const { return ErrorCount; }
  unsigned warningCount() const { return WarningCount; }
  bool reachedLimit() const { return ErrorLimit && ErrorCount >= ErrorLimit; }

  /// Status lines: `○ Beginning build.` / `● Build failed.`
  void status(const std::string &text);
  void statusOk(const std::string &text);
  void statusFail(const std::string &text);

  const SourceManager &sources() const { return SM; }

  /// Records that a macro was invoked over `at`, and what it stood for.
  ///
  /// Expanded tokens all carry the invocation's range, so an error inside one
  /// points at the call — but the code it is really about appears nowhere in
  /// the file. A diagnostic landing here is shown the expansion.
  void noteExpansion(SourceRange at, std::string macro, std::string expansion) {
    std::lock_guard<std::mutex> lock(Mutex);
    Expansions.push_back({at, std::move(macro), std::move(expansion)});
  }

  /// Every range a macro was invoked over: what an editor must not write
  /// into, since the code there is the macro's to make.
  std::vector<SourceRange> expansionRanges() const {
    std::lock_guard<std::mutex> lock(Mutex);
    std::vector<SourceRange> out;
    for (const Expansion &e : Expansions)
      out.push_back(e.At);
    return out;
  }

private:
  /// One depth per thread. A static is right here: speculation is a property
  /// of the parse in progress, and there is one engine per compilation.
  static unsigned &suppressDepth() {
    static thread_local unsigned depth = 0;
    return depth;
  }
  /// Where this thread's diagnostics go while a pass is collecting them.
  static std::vector<Diagnostic> *&captureSink() {
    static thread_local std::vector<Diagnostic> *sink = nullptr;
    return sink;
  }

  /// The notes an expansion at `r` adds, outermost first.
  std::vector<std::string> expansionNotes(SourceRange r) const;

  DiagBuilder make(Severity sev, SourceRange r, std::string msg) {
    Diagnostic d;
    d.Sev = sev;
    d.Range = r;
    d.Message = std::move(msg);
    return DiagBuilder(this, std::move(d));
  }

  /// How one snippet block is decorated. `HeaderPrefix` opens the block,
  /// `BodyPrefix` runs down its left edge.
  struct SnippetStyle {
    std::string HeaderPrefix; ///< e.g. "┌ ", "│ " or ""
    std::string BodyPrefix;   ///< e.g. "│ " or "  "
    std::string LabelPrefix;  ///< e.g. "└▶", "├▶" or "" (no label line)
    std::string Label;        ///< text on the LabelPrefix line
    bool UseCaret = true;     ///< carets (^^^) vs. an arrow rule (───>)
  };

  /// `d` as one line of JSON, for `--diagnostic-format json`.
  void emitJson(std::ostream &os, const Diagnostic &d, Severity sev);
  /// `d` as `file:line:col: severity: message`, for `--diagnostic-format short`.
  void emitShort(std::ostream &os, const Diagnostic &d, Severity sev);

  void renderSnippet(std::ostream &os, SourceRange range, Severity sev,
                     const std::string &caretMessage,
                     const std::string &trailingHint, const SnippetStyle &style,
                     unsigned gutterWidth);

  const SourceManager &SM;
  bool Color = false;
  bool WarnAsError = false;
  bool QuietWarnings = false;
  bool Json = false;
  bool Short = false;
  bool Silent = false;
  bool Holding = false;
  std::ostringstream Held;
  /// Where output goes: the terminal, or the held buffer. Call with `Mutex`
  /// held.
  std::ostream &out() { return Holding ? static_cast<std::ostream &>(Held) : std::cerr; }
  unsigned ErrorLimit = 0;
  std::atomic<unsigned> ErrorCount{0};
  std::atomic<unsigned> WarningCount{0};
  bool LimitReported = false;
  /// Held while a diagnostic is rendered, so two threads reporting at once
  /// produce two diagnostics rather than one interleaved mess.
  mutable std::mutex Mutex;

  struct Expansion {
    SourceRange At;
    std::string Macro;
    std::string Text;
  };
  std::vector<Expansion> Expansions;
};

} // namespace rune

#endif
