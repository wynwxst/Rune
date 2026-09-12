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
#include <sstream>
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
inline void toStream(std::ostringstream &os, const std::string &v) { os << v; }
inline void toStream(std::ostringstream &os, const char *v) { os << v; }
inline void toStream(std::ostringstream &os, char v) { os << v; }
template <typename T> void toStream(std::ostringstream &os, const T &v) { os << v; }

/// No arguments left, so every remaining brace is a literal one. `{{` and
/// `}}` still stand for `{` and `}`: a message means the same thing whether or
/// not it happens to have run out of arguments before reaching them.
inline void formatInto(std::ostringstream &os, const char *pattern) {
  for (const char *p = pattern; *p; ++p) {
    if ((p[0] == '{' && p[1] == '{') || (p[0] == '}' && p[1] == '}')) {
      os << *p;
      ++p;
      continue;
    }
    os << *p;
  }
}

template <typename T, typename... Rest>
void formatInto(std::ostringstream &os, const char *pattern, const T &arg,
                const Rest &...rest) {
  for (const char *p = pattern; *p; ++p) {
    if (p[0] == '{' && p[1] == '}') {
      toStream(os, arg);
      formatInto(os, p + 2, rest...);
      return;
    }
    if ((p[0] == '{' && p[1] == '{') || (p[0] == '}' && p[1] == '}')) {
      os << *p;
      ++p;
      continue;
    }
    os << *p;
  }
}
} // namespace detail

/// `fmt("expected {} — got {}", a, b)`
///
/// Named `fmt` rather than `format` so that argument-dependent lookup on a
/// `std::string` argument cannot drag in `std::format` and make the call
/// ambiguous.
template <typename... Args>
std::string fmt(const char *pattern, const Args &...args) {
  std::ostringstream os;
  detail::formatInto(os, pattern, args...);
  return os.str();
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

  void renderSnippet(std::ostream &os, SourceRange range, Severity sev,
                     const std::string &caretMessage,
                     const std::string &trailingHint, const SnippetStyle &style,
                     unsigned gutterWidth);

  const SourceManager &SM;
  bool Color = false;
  bool WarnAsError = false;
  bool QuietWarnings = false;
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
