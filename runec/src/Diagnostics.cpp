#include "rune/Diagnostics.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <unistd.h>

namespace rune {

//===----------------------------------------------------------------------===//
// Colour handling
//===----------------------------------------------------------------------===//

namespace {
struct Palette {
  const char *Reset, *Bold, *Dim;
  const char *Red, *BoldRed, *Yellow, *BoldYellow;
  const char *Blue, *Cyan, *Green, *Magenta;
};

const Palette kColored = {
    "\x1b[0m",  "\x1b[1m",  "\x1b[2m",  "\x1b[31m", "\x1b[1;31m", "\x1b[33m",
    "\x1b[1;33m", "\x1b[34m", "\x1b[36m", "\x1b[32m", "\x1b[35m"};
const Palette kPlain = {"", "", "", "", "", "", "", "", "", "", ""};

/// Number of terminal cells `text` occupies, treating UTF-8 continuation bytes
/// as zero-width and expanding tabs to the next multiple of 4.
unsigned displayWidth(const std::string &text, size_t byteCount) {
  unsigned w = 0;
  for (size_t i = 0; i < byteCount && i < text.size(); ++i) {
    unsigned char c = static_cast<unsigned char>(text[i]);
    if (c == '\t')
      w += 4 - (w % 4);
    else if ((c & 0xC0) != 0x80)
      w += 1;
  }
  return w;
}

std::string expandTabs(const std::string &text) {
  std::string out;
  unsigned w = 0;
  for (char ch : text) {
    if (ch == '\t') {
      unsigned pad = 4 - (w % 4);
      out.append(pad, ' ');
      w += pad;
    } else {
      out.push_back(ch);
      if ((static_cast<unsigned char>(ch) & 0xC0) != 0x80)
        ++w;
    }
  }
  return out;
}

std::string repeat(const char *unit, unsigned n) {
  std::string s;
  s.reserve(n * 3);
  for (unsigned i = 0; i < n; ++i)
    s += unit;
  return s;
}
} // namespace

std::string DiagCode::str() const {
  if (!Value)
    return {};
  char buf[8];
  snprintf(buf, sizeof(buf), "%c%04u", isWarning ? 'W' : 'E', Value);
  return std::string(buf);
}

void DiagnosticEngine::detectColor() {
  const char *noColor = getenv("NO_COLOR");
  const char *term = getenv("TERM");
  Color = isatty(STDERR_FILENO) && !noColor && (!term || std::string(term) != "dumb");
}

DiagBuilder::~DiagBuilder() {
  if (Engine)
    Engine->emit(D);
}

//===----------------------------------------------------------------------===//
// Rendering
//===----------------------------------------------------------------------===//

namespace {
const char *severityWord(Severity s) {
  switch (s) {
  case Severity::Note: return "note";
  case Severity::Remark: return "remark";
  case Severity::Warning: return "WARNING";
  case Severity::Error: return "ERROR";
  case Severity::Fatal: return "FATAL";
  }
  return "ERROR";
}

const char *severityColor(Severity s, const Palette &P) {
  switch (s) {
  case Severity::Note: return P.Cyan;
  case Severity::Remark: return P.Blue;
  case Severity::Warning: return P.BoldYellow;
  case Severity::Error:
  case Severity::Fatal: return P.BoldRed;
  }
  return P.BoldRed;
}

/// Filled bullet for anything that fails or warns, hollow for informational.
const char *severityGlyph(Severity s) {
  return (s == Severity::Error || s == Severity::Fatal || s == Severity::Warning)
             ? "●"
             : "○";
}
} // namespace

void DiagnosticEngine::renderSnippet(std::ostream &os, SourceRange range,
                                     Severity sev,
                                     const std::string &caretMessage,
                                     const std::string &trailingHint,
                                     const SnippetStyle &style,
                                     unsigned gutterWidth) {
  const Palette &P = Color ? kColored : kPlain;
  PresumedLoc pl = SM.decode(range.begin());

  // ---- header: `● file.rune [line:colStart..colEnd]` ---------------------
  unsigned endCol = pl.Column + std::max(1u, range.length());
  {
    PresumedLoc endPl = SM.decode(range.end());
    if (endPl.isValid() && endPl.File == pl.File && endPl.Line == pl.Line)
      endCol = endPl.Column;
  }
  os << style.HeaderPrefix << severityColor(sev, P) << severityGlyph(sev)
     << P.Reset << " " << P.Bold << (pl.isValid() ? pl.File->Name : "<unknown>")
     << P.Reset << " " << P.Dim << "[" << pl.Line << ":" << pl.Column << ".."
     << endCol << "]" << P.Reset << "\n";

  if (!style.LabelPrefix.empty())
    os << severityColor(sev, P) << style.LabelPrefix << P.Reset << " " << P.Bold
       << style.Label << P.Reset << "\n";

  if (!pl.isValid())
    return;

  // ---- source lines with a `NN ║ ` gutter --------------------------------
  const SourceFile &F = *pl.File;
  unsigned first = pl.Line > 1 ? pl.Line - 1 : pl.Line;
  unsigned last = std::min<unsigned>(pl.Line + 1,
                                     static_cast<unsigned>(F.LineStarts.size()));

  auto gutter = [&](unsigned lineNo) {
    std::string num = std::to_string(lineNo);
    std::string pad(gutterWidth > num.size() ? gutterWidth - num.size() : 0, ' ');
    return pad + P.Blue + num + P.Reset + " " + P.Dim + "║" + P.Reset + " ";
  };
  // Blank gutter of the same visible width, for caret/note rows.
  std::string blankGutter(gutterWidth + 3, ' ');

  for (unsigned ln = first; ln <= last; ++ln) {
    std::string raw = SM.lineText(F, ln);
    os << style.BodyPrefix << gutter(ln) << expandTabs(raw) << "\n";

    if (ln != pl.Line)
      continue;

    // The marker row sits directly beneath the offending line.
    unsigned startCol = displayWidth(raw, pl.Column);
    unsigned width = std::max(1u, displayWidth(raw, endCol) - startCol);
    if (endCol <= pl.Column)
      width = 1;

    os << style.BodyPrefix << blankGutter << std::string(startCol, ' ');
    if (style.UseCaret) {
      os << severityColor(sev, P) << repeat("^", width) << P.Reset;
      if (!caretMessage.empty())
        os << " " << severityColor(sev, P) << severityWord(sev) << ":" << P.Reset
           << " " << caretMessage;
    } else {
      // Arrow rule form: `─────────> hint: ...`
      unsigned rule = std::max(width, 3u) + 9;
      os << P.Dim << repeat("─", rule) << ">" << P.Reset;
      if (!trailingHint.empty())
        os << " " << P.Bold << P.Magenta << "hint:" << P.Reset << " "
           << trailingHint;
    }
    os << "\n";
  }
}

/// Shortens a long expansion to something a note can carry on one line.
static std::string elide(const std::string &s, size_t width = 96) {
  if (s.size() <= width)
    return s;
  return s.substr(0, width - 1) + "\u2026";
}

std::vector<std::string>
DiagnosticEngine::expansionNotes(SourceRange r) const {
  std::vector<std::string> notes;
  if (!r.isValid())
    return notes;
  // A macro whose body calls another produces a chain, outermost first. Long
  // ones say more about the macros than about the error, so show the ends.
  constexpr size_t kMaxLinks = 4;
  std::vector<const Expansion *> chain;
  for (const Expansion &e : Expansions) {
    // Everything an expansion produced carries the invocation's range, so the
    // diagnostic starts exactly where the macro was called.
    if (!e.At.isValid() || r.begin() < e.At.begin() || e.At.end() <= r.begin())
      continue;
    // One call site can expand the same macro on the same arguments more than
    // once; saying so twice tells the reader nothing new.
    bool seen = false;
    for (const Expansion *p : chain)
      seen |= p->Macro == e.Macro && p->Text == e.Text;
    if (!seen)
      chain.push_back(&e);
  }
  for (size_t i = 0; i < chain.size(); ++i) {
    if (chain.size() > kMaxLinks && i == kMaxLinks - 1) {
      notes.push_back("\u2026 through " +
                      std::to_string(chain.size() - kMaxLinks + 1) +
                      " more expansions");
      i = chain.size() - 1;
    }
    notes.push_back("in the expansion of `" + elide(chain[i]->Macro) + "`");
    notes.push_back("  which stands for: " + elide(chain[i]->Text));
  }
  return notes;
}

void DiagnosticEngine::emit(const Diagnostic &incoming) {
  if (Silent) {
    if (incoming.Sev == Severity::Error || incoming.Sev == Severity::Fatal)
      ++ErrorCount;
    else if (incoming.Sev == Severity::Warning)
      ++WarningCount;
    return;
  }
  if (suppressDepth())
    return;
  // A pass collecting its findings for later: set aside verbatim, so that
  // replaying them is the same as having reported them in the first place.
  if (std::vector<Diagnostic> *sink = captureSink()) {
    sink->push_back(incoming);
    return;
  }
  // Everything below this point either counts a diagnostic or writes one out,
  // and files are parsed on several threads at once.
  std::lock_guard<std::mutex> lock(Mutex);
  Severity sev = incoming.Sev;
  if (sev == Severity::Warning && WarnAsError)
    sev = Severity::Error;
  if (sev == Severity::Warning && QuietWarnings)
    return;

  if (sev == Severity::Error || sev == Severity::Fatal) {
    ++ErrorCount;
    if (ErrorLimit && ErrorCount > ErrorLimit) {
      if (!LimitReported && !Json && !Short) {
        LimitReported = true;
        const Palette &P = Color ? kColored : kPlain;
        out() << P.BoldRed << "●" << P.Reset << " too many errors emitted; "
                  << "stopping after " << ErrorLimit << ".\n";
      }
      return;
    }
  } else if (sev == Severity::Warning) {
    ++WarningCount;
  }

  Diagnostic d = incoming;
  // An error inside expanded code needs the expansion to make sense of it:
  // the tokens it is about were never written in the file.
  if (sev != Severity::Note && sev != Severity::Remark) {
    std::vector<std::string> exp = expansionNotes(d.Range);
    d.Notes.insert(d.Notes.end(), exp.begin(), exp.end());
  }

  if (Json) {
    emitJson(out(), d, sev);
    return;
  }
  if (Short) {
    emitShort(out(), d, sev);
    return;
  }

  const Palette &P = Color ? kColored : kPlain;
  std::ostream &os = out();

  std::string message = d.Message;
  if (d.Code)
    message += P.Dim + std::string(" [") + d.Code.str() + "]" + P.Reset;

  // Locationless diagnostics are a single line.
  if (!d.Range.isValid()) {
    os << severityColor(sev, P) << severityGlyph(sev) << " " << severityWord(sev)
       << ":" << P.Reset << " " << message << "\n";
    for (const auto &n : d.Notes)
      os << "  " << P.Dim << "─" << P.Reset << "  " << P.Bold
         << "note:" << P.Reset << " " << n << "\n";
    return;
  }

  // Width of the widest line number across every snippet we are about to draw.
  unsigned gutterWidth = 1;
  auto widen = [&](SourceRange r) {
    PresumedLoc pl = SM.decode(r.begin());
    if (pl.isValid())
      gutterWidth = std::max<unsigned>(
          gutterWidth, static_cast<unsigned>(std::to_string(pl.Line + 1).size()));
  };
  widen(d.Range);
  for (const auto &rel : d.Related)
    widen(rel.Range);

  const bool boxed = !d.Related.empty();
  SnippetStyle primary;
  primary.HeaderPrefix = boxed ? "┌ " : "";  // ┌
  primary.BodyPrefix = boxed ? "│ " : "";    // │
  primary.UseCaret = true;
  renderSnippet(os, d.Range, sev, message, {}, primary, gutterWidth);

  std::string notePrefix = boxed ? "│ " : "";
  std::string blankGutter(gutterWidth + 3, ' ');
  for (const auto &n : d.Notes)
    os << notePrefix << blankGutter << P.Dim << "─" << P.Reset << "  "
       << P.Bold << "note:" << P.Reset << " " << n << "\n";

  for (size_t i = 0; i < d.Related.size(); ++i) {
    const RelatedInfo &rel = d.Related[i];
    bool isLast = i + 1 == d.Related.size();

    os << "│\n"; // spacer keeping the vertical rule unbroken

    SnippetStyle rs;
    rs.HeaderPrefix = "│ ";                       // │
    rs.LabelPrefix = isLast ? "└▶" : "├▶"; // └▶ / ├▶
    rs.Label = rel.Label;
    rs.BodyPrefix = isLast ? "  " : "│ ";
    rs.UseCaret = false;
    renderSnippet(os, rel.Range, rel.Sev, {}, rel.Hint, rs, gutterWidth);
  }
}

//===----------------------------------------------------------------------===//
// Status lines
//===----------------------------------------------------------------------===//

namespace {
void jsonString(std::ostream &os, const std::string &s) {
  os << '"';
  for (unsigned char c : s) {
    switch (c) {
    case '"': os << "\\\""; break;
    case '\\': os << "\\\\"; break;
    case '\n': os << "\\n"; break;
    case '\r': os << "\\r"; break;
    case '\t': os << "\\t"; break;
    default:
      if (c < 0x20) {
        static const char *hex = "0123456789abcdef";
        os << "\\u00" << hex[c >> 4] << hex[c & 15];
      } else {
        os << c;
      }
    }
  }
  os << '"';
}

const char *severityName(Severity s) {
  switch (s) {
  case Severity::Note: return "note";
  case Severity::Remark: return "remark";
  case Severity::Warning: return "warning";
  case Severity::Error: return "error";
  case Severity::Fatal: return "fatal";
  }
  return "error";
}
} // namespace

/// `"file":…,"line":…` for `r`: the path made absolute, a 1-based line and a
/// 0-based byte column, for both ends. Nothing at all for an invalid range.
static void jsonRange(std::ostream &os, const SourceManager &SM, SourceRange r) {
  PresumedLoc b = SM.decode(r.begin());
  if (!b.isValid())
    return;
  PresumedLoc e = SM.decode(r.end());
  if (!e.isValid())
    e = b;
  std::error_code ec;
  std::filesystem::path path = std::filesystem::absolute(b.File->Path, ec);
  os << ",\"file\":";
  jsonString(os, ec ? b.File->Path : path.lexically_normal().string());
  os << ",\"line\":" << b.Line << ",\"column\":" << b.Column
     << ",\"endLine\":" << e.Line << ",\"endColumn\":" << e.Column;
}

void DiagnosticEngine::emitJson(std::ostream &os, const Diagnostic &d,
                                Severity sev) {
  os << "{\"severity\":\"" << severityName(sev) << "\"";
  if (d.Code) {
    os << ",\"code\":";
    jsonString(os, d.Code.str());
  }
  os << ",\"message\":";
  jsonString(os, d.Message);
  jsonRange(os, SM, d.Range);
  os << ",\"notes\":[";
  for (size_t i = 0; i < d.Notes.size(); ++i) {
    if (i) os << ",";
    jsonString(os, d.Notes[i]);
  }
  os << "],\"related\":[";
  bool first = true;
  for (const RelatedInfo &rel : d.Related) {
    if (!SM.decode(rel.Range.begin()).isValid())
      continue;
    if (!first) os << ",";
    first = false;
    std::string text = rel.Label;
    if (!rel.Hint.empty())
      text += text.empty() ? rel.Hint : ": " + rel.Hint;
    os << "{\"message\":";
    jsonString(os, text);
    jsonRange(os, SM, rel.Range);
    os << "}";
  }
  os << "]}\n";
}

/// `file:line:col` for `r`, with a 1-based column as editors count them, or
/// "" for an invalid range.
static std::string shortPlace(const SourceManager &SM, SourceRange r) {
  PresumedLoc b = SM.decode(r.begin());
  if (!b.isValid())
    return "";
  std::error_code ec;
  std::filesystem::path path = std::filesystem::absolute(b.File->Path, ec);
  return (ec ? b.File->Path : path.lexically_normal().string()) + ":" +
         std::to_string(b.Line) + ":" + std::to_string(b.Column + 1);
}

void DiagnosticEngine::emitShort(std::ostream &os, const Diagnostic &d,
                                 Severity sev) {
  std::string place = shortPlace(SM, d.Range);
  std::string lead = place.empty() ? std::string("runec") : place;
  os << lead << ": " << severityName(sev) << ": " << d.Message;
  if (d.Code)
    os << " [" << d.Code.str() << "]";
  os << "\n";
  for (const std::string &n : d.Notes)
    os << lead << ": note: " << n << "\n";
  for (const RelatedInfo &rel : d.Related) {
    std::string at = shortPlace(SM, rel.Range);
    if (at.empty())
      continue;
    std::string text = rel.Label;
    if (!rel.Hint.empty())
      text += text.empty() ? rel.Hint : ": " + rel.Hint;
    os << at << ": note: " << text << "\n";
  }
}

void DiagnosticEngine::status(const std::string &text) {
  if (Json || Short)
    return;
  const Palette &P = Color ? kColored : kPlain;
  std::lock_guard<std::mutex> lock(Mutex);
  out() << P.Green << "○" << P.Reset << " " << text << "\n";
}

void DiagnosticEngine::statusOk(const std::string &text) {
  if (Json || Short)
    return;
  const Palette &P = Color ? kColored : kPlain;
  std::lock_guard<std::mutex> lock(Mutex);
  out() << P.Green << "●" << P.Reset << " " << text << "\n";
}

void DiagnosticEngine::statusFail(const std::string &text) {
  if (Json || Short)
    return;
  const Palette &P = Color ? kColored : kPlain;
  std::lock_guard<std::mutex> lock(Mutex);
  out() << P.BoldRed << "●" << P.Reset << " " << text << "\n";
}

void DiagnosticEngine::release() {
  std::lock_guard<std::mutex> lock(Mutex);
  if (!Holding)
    return;
  Holding = false;
  std::cerr << Held.str();
  std::cerr.flush();
  Held.str(std::string());
}

} // namespace rune
