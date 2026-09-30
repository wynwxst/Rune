//===- MacroEval.cpp - Running a procedural macro while compiling ---------===//
//
// Three jobs: recognising a macro package, recording what it declares, and
// running it when an invocation names one of its macros.
//
// The running is deliberately dull. The compiler writes a request file, runs
// the package as an ordinary program, and reads an answer file. Nothing is
// loaded into the compiler's own address space, so a macro that crashes or
// loops is a failed expansion rather than a failed compiler — and a macro
// package can be run by hand, which is the difference between a mechanism you
// can debug and one you can only wonder about.
//
//===----------------------------------------------------------------------===//
#include "rune/MacroEval.h"

#include "rune/Lexer.h"
#include "rune/Source.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

#if defined(_WIN32)
#include <process.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

namespace rune {
namespace {

bool isLayoutTok(const Token &t) { return t.Kind == Tok::Newline; }

bool opensGroup(Tok k) {
  return k == Tok::LParen || k == Tok::LBracket || k == Tok::LBrace;
}
bool closesGroup(Tok k) {
  return k == Tok::RParen || k == Tok::RBracket || k == Tok::RBrace;
}

const char *groupKind(Tok open) {
  switch (open) {
  case Tok::LBrace: return "block";
  case Tok::LBracket: return "brackets";
  default: return "parens";
  }
}

const char *kindName(const Token &t) {
  switch (t.Kind) {
  case Tok::Identifier: return "name";
  case Tok::IntLiteral:
  case Tok::FloatLiteral: return "number";
  case Tok::StringLiteral: return "string";
  case Tok::CharLiteral: return "character";
  default: break;
  }
  return isKeyword(t.Kind) ? "keyword" : "punctuation";
}

/// A token's spelling. A string literal gives its contents, because that is
/// what a macro reading one wants; `Macro::string` puts the quotes back.
std::string spellOne(const Token &t) {
  switch (t.Kind) {
  case Tok::Identifier:
  case Tok::StringLiteral:
    return t.Text;
  case Tok::IntLiteral:
    return t.Text.empty() ? std::to_string(t.IntValue) : t.Text;
  case Tok::FloatLiteral: {
    if (!t.Text.empty())
      return t.Text;
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", t.FloatValue);
    return buf;
  }
  case Tok::CharLiteral:
    return t.Text;
  default:
    return tokenSpelling(t.Kind);
  }
}

/// A run written out as source, spaced so it lexes back to itself.
std::string spellRun(const std::vector<Token> &run, size_t from, size_t to) {
  std::string out;
  for (size_t i = from; i < to && i < run.size(); ++i) {
    const Token &t = run[i];
    if (isLayoutTok(t) || t.Kind == Tok::EndOfFile)
      continue;
    if (!out.empty())
      out += ' ';
    if (t.Kind == Tok::StringLiteral) {
      out += '"';
      for (char c : t.Text) {
        if (c == '"' || c == '\\')
          out += '\\';
        if (c == '\n') {
          out += "\\n";
          continue;
        }
        out += c;
      }
      out += '"';
      continue;
    }
    out += spellOne(t);
  }
  return out;
}

/// Tabs and newlines separate the fields of a request, so the text a token
/// carries says so rather than breaking the format.
std::string escapeField(const std::string &s) {
  std::string out;
  for (char c : s) {
    switch (c) {
    case '\\': out += "\\\\"; break;
    case '\n': out += "\\n"; break;
    case '\t': out += "\\t"; break;
    case '\r': out += "\\r"; break;
    default: out += c;
    }
  }
  return out;
}

/// Writes one token, and its children when it is a bracketed group, as
///
///     <kind>\t<text>\t<children>
///
/// followed by the children themselves. A group is one token whose text is
/// the whole group — brackets and all — so a macro can hand a body back
/// untouched, and whose children are what is inside it, so a macro that wants
/// to look need not lex anything.
size_t writeToken(const std::vector<Token> &run, size_t i, std::string &out) {
  const Token &t = run[i];
  if (opensGroup(t.Kind)) {
    // Find the matching bracket.
    int depth = 0;
    size_t end = i;
    for (; end < run.size(); ++end) {
      if (opensGroup(run[end].Kind))
        ++depth;
      else if (closesGroup(run[end].Kind) && --depth == 0) {
        ++end;
        break;
      }
    }
    // Count the children at the top level of the group.
    std::vector<size_t> starts;
    for (size_t k = i + 1; k + 1 < end;) {
      if (isLayoutTok(run[k])) {
        ++k;
        continue;
      }
      starts.push_back(k);
      if (opensGroup(run[k].Kind)) {
        int d = 0;
        size_t j = k;
        for (; j < end; ++j) {
          if (opensGroup(run[j].Kind))
            ++d;
          else if (closesGroup(run[j].Kind) && --d == 0) {
            ++j;
            break;
          }
        }
        k = j;
      } else {
        ++k;
      }
    }
    const std::string whole =
        std::string(tokenSpelling(t.Kind)) + " " +
        spellRun(run, i + 1, end > 0 ? end - 1 : 0) + " " +
        (end > 0 ? tokenSpelling(run[end - 1].Kind) : "");
    out += groupKind(t.Kind);
    out += '\t';
    out += escapeField(whole);
    out += '\t';
    out += std::to_string(starts.size());
    out += '\n';
    for (size_t start : starts)
      writeToken(run, start, out);
    return end;
  }

  out += kindName(t);
  out += '\t';
  out += escapeField(spellOne(t));
  out += "\t0\n";
  return i + 1;
}

std::string serialiseRequest(const std::string &name,
                             const std::vector<Token> &input) {
  std::string out = name + "\n";
  for (size_t i = 0; i < input.size();) {
    if (isLayoutTok(input[i]) || input[i].Kind == Tok::EndOfFile) {
      ++i;
      continue;
    }
    i = writeToken(input, i, out);
  }
  return out;
}

/// The text a macro handed back, lexed. Every token is given the invocation's
/// own location, so a mistake in generated code is reported where the macro
/// was written rather than at an offset into a string nobody can see.
std::vector<Token> lexText(const std::string &text, SourceRange at, bool &ok) {
  SourceManager scratch;
  DiagnosticEngine quiet(scratch);
  // Silent, not speculating: speculation belongs to the thread, so it went
  // on swallowing every diagnostic after this one — and it does not count
  // the errors `ok` has to see.
  quiet.setSilent(true);
  unsigned id = scratch.addBuffer("<macro>", text);
  std::vector<Token> out = Lexer(scratch, quiet, id).tokenize();
  ok = !quiet.hadError();
  while (!out.empty() &&
         (out.back().Kind == Tok::EndOfFile || isLayoutTok(out.back())))
    out.pop_back();
  for (Token &t : out)
    t.Range = at;
  return out;
}

int runWithFiles(const std::string &program, const std::string &request,
                 const std::string &answer) {
#if defined(_WIN32)
  std::string command = "\"" + program + "\"";
  _putenv_s("RUNE_MACRO_REQUEST", request.c_str());
  _putenv_s("RUNE_MACRO_ANSWER", answer.c_str());
  return std::system(command.c_str());
#else
  setenv("RUNE_MACRO_REQUEST", request.c_str(), 1);
  setenv("RUNE_MACRO_ANSWER", answer.c_str(), 1);
  std::vector<char *> argv{const_cast<char *>(program.c_str()), nullptr};
  pid_t pid = 0;
  if (posix_spawn(&pid, program.c_str(), nullptr, nullptr, argv.data(),
                  environ) != 0)
    return 127;
  int status = 0;
  if (waitpid(pid, &status, 0) < 0)
    return 127;
  if (WIFSIGNALED(status))
    return 128 + WTERMSIG(status);
  return WIFEXITED(status) ? WEXITSTATUS(status) : 127;
#endif
}

std::filesystem::path scratchFile(const char *suffix) {
  static std::atomic<unsigned> counter{0};
  std::filesystem::path dir = std::filesystem::temp_directory_path();
  return dir / ("rune-macro-" + std::to_string(
#if defined(_WIN32)
                                    _getpid()
#else
                                    getpid()
#endif
                                    ) +
                "-" + std::to_string(counter++) + suffix);
}

/// True when the attribute at `i` is `@macro`. `macro` is a keyword, so it
/// arrives as one rather than as a name.
bool isMacroAttribute(const std::vector<Token> &t, size_t i) {
  return i + 1 < t.size() && t[i].Kind == Tok::At &&
         (t[i + 1].Kind == Tok::KwMacro ||
          (t[i + 1].Kind == Tok::Identifier && t[i + 1].Text == "macro"));
}

} // namespace

bool declaresMacroPackage(const std::vector<Token> &toks) {
  // `@type(Macros)` is a file directive, so it stands before everything but
  // layout and other directives. Looking only at the head keeps an `@type`
  // written further down — which is a mistake the parser reports — from
  // quietly deciding what the file is.
  for (size_t i = 0; i + 3 < toks.size(); ++i) {
    if (isLayoutTok(toks[i]))
      continue;
    if (toks[i].Kind != Tok::At)
      return false;
    if (toks[i + 1].Kind != Tok::KwType &&
        !(toks[i + 1].Kind == Tok::Identifier && toks[i + 1].Text == "type")) {
      // Some other directive — `@link`, `@linkpath` — so keep looking.
      while (i < toks.size() && toks[i].Kind != Tok::RParen)
        ++i;
      continue;
    }
    if (toks[i + 2].Kind == Tok::LParen &&
        toks[i + 3].Kind == Tok::Identifier)
      return toks[i + 3].Text == "Macros";
    return false;
  }
  return false;
}

void collectProcMacros(const std::vector<Token> &toks,
                       DiagnosticEngine &diags, ProcMacroTable &into,
                       const std::string &module) {
  for (size_t i = 0; i + 1 < toks.size(); ++i) {
    if (!isMacroAttribute(toks, i))
      continue;
    size_t j = i + 2;
    bool isPublic = false;
    while (j < toks.size() &&
           (isLayoutTok(toks[j]) || toks[j].Kind == Tok::KwPub)) {
      if (toks[j].Kind == Tok::KwPub)
        isPublic = true;
      ++j;
    }
    if (j >= toks.size() || toks[j].Kind != Tok::KwFn) {
      diags.error(toks[i].Range, "`@macro` belongs on a function")
          .note("a procedural macro is a `fn` taking and returning "
                "`Macro::Tokens`")
          .code(125);
      continue;
    }
    size_t nameAt = j + 1;
    while (nameAt < toks.size() && isLayoutTok(toks[nameAt]))
      ++nameAt;
    if (nameAt >= toks.size() || toks[nameAt].Kind != Tok::Identifier)
      continue;

    ProcMacro m;
    m.Name = toks[nameAt].Text;
    m.Range = toks[nameAt].Range;
    m.Module = module;
    m.Doc = toks[i].Doc.empty() ? toks[j].Doc : toks[i].Doc;
    if (!isPublic) {
      diags.error(m.Range, "a macro has to be `pub`")
          .note("the program it expands into is not this package, so the "
                "dispatcher the compiler writes has to be able to see it")
          .note("write `@macro\\npub fn {}(...)`", m.Name)
          .code(128);
      continue;
    }
    auto existing = into.find(m.Name);
    if (existing != into.end()) {
      diags.error(m.Range, "macro '{}' is declared twice", m.Name)
          .related(existing->second.Range, "the earlier declaration")
          .code(126);
      continue;
    }
    into[m.Name] = std::move(m);
  }
}

void rejectProcMacros(std::vector<Token> &toks, DiagnosticEngine &diags) {
  bool sawOne = false;
  for (size_t i = 0; i + 1 < toks.size(); ++i)
    if (isMacroAttribute(toks, i)) {
      sawOne = true;
      break;
    }
  if (!sawOne)
    return;

  std::vector<Token> kept;
  kept.reserve(toks.size());
  for (size_t i = 0; i < toks.size();) {
    if (!isMacroAttribute(toks, i)) {
      kept.push_back(toks[i++]);
      continue;
    }
    diags.error(toks[i].Range,
                "a procedural macro belongs in a macro package")
        .note("a `@macro fn` is built and run while this program is compiled, "
              "so it lives in a file of its own that opens with "
              "`@type(Macros)`")
        .note("nothing that file imports or declares reaches this one; only "
              "the tokens its macros hand back do")
        .code(129);
    // Skip the whole function so the grammar does not trip over what is left.
    size_t j = i + 2;
    while (j < toks.size() && toks[j].Kind != Tok::LBrace)
      ++j;
    if (j < toks.size()) {
      int depth = 0;
      for (; j < toks.size(); ++j) {
        if (opensGroup(toks[j].Kind))
          ++depth;
        else if (closesGroup(toks[j].Kind) && --depth == 0) {
          ++j;
          break;
        }
      }
    }
    i = j;
  }
  toks.swap(kept);
}

bool runProcMacro(const ProcMacro &m, const std::string &program,
                  const std::vector<Token> &input, SourceRange at,
                  DiagnosticEngine &diags, std::vector<Token> &out) {
  if (program.empty()) {
    diags.error(at, "macro '{}' has no package to run", m.Name).code(124);
    return false;
  }

  const std::filesystem::path requestPath = scratchFile(".request");
  const std::filesystem::path answerPath = scratchFile(".answer");
  {
    std::ofstream f(requestPath, std::ios::binary);
    if (!f) {
      diags.error(at, "cannot write what macro '{}' is to be given", m.Name)
          .note("the file would have been '{}'", requestPath.string())
          .code(124);
      return false;
    }
    f << serialiseRequest(m.Name, input);
  }

  struct Cleanup {
    std::filesystem::path A, B;
    ~Cleanup() {
      std::error_code ec;
      std::filesystem::remove(A, ec);
      std::filesystem::remove(B, ec);
    }
  } cleanup{requestPath, answerPath};

  int rc = runWithFiles(program, requestPath.string(), answerPath.string());
  if (rc != 0) {
    auto d = diags.error(at, "macro '{}' did not finish", m.Name);
    if (rc >= 128)
      d.note("the macro package stopped on signal {}", rc - 128);
    else
      d.note("the macro package exited with status {}", rc);
    d.note("run it yourself to see why: RUNE_MACRO_REQUEST=... "
           "RUNE_MACRO_ANSWER=... {}", program);
    d.related(m.Range, "this is the macro that was running");
    d.code(124);
    return false;
  }

  std::ifstream answerFile(answerPath, std::ios::binary);
  if (!answerFile) {
    diags.error(at, "macro '{}' wrote nothing back", m.Name).code(124);
    return false;
  }
  std::stringstream buffer;
  buffer << answerFile.rdbuf();
  std::string answer = buffer.str();

  const size_t split = answer.find('\n');
  const std::string status = answer.substr(0, split);
  const std::string body =
      split == std::string::npos ? std::string() : answer.substr(split + 1);

  if (status == "error") {
    auto d = diags.error(at, "{}", body.empty() ? "this macro refused what it "
                                                  "was given"
                                                : body);
    d.related(m.Range, "the macro says so here");
    d.code(124);
    return false;
  }
  if (status != "ok") {
    diags.error(at, "macro '{}' answered with something unexpected", m.Name)
        .note("the first line should say `ok` or `error`")
        .code(124);
    return false;
  }

  bool lexed = true;
  std::vector<Token> produced = lexText(body, at, lexed);
  if (!lexed) {
    diags.error(at, "macro '{}' produced text that is not valid Rune", m.Name)
        .related(m.Range, "the macro is here")
        .code(124);
    return false;
  }
  out.insert(out.end(), produced.begin(), produced.end());
  return true;
}

std::string macroDispatcherSource(const ProcMacroTable &macros,
                                  const std::vector<std::string> &modules) {
  std::string src =
      "// Written by the compiler: what turns a macro package into a program.\n"
      "//\n"
      "// The compiler names a macro; this hands the name to the function it\n"
      "// belongs to, and `Macro::serve` does the reading and writing around\n"
      "// it. There is nothing here worth editing — the file is made fresh\n"
      "// every time the package is built.\n"
      "import std::Macro\n";
  for (const std::string &mod : modules)
    src += "import " + mod + "\n";
  src += "\nfn dispatch(name: String, input: Macro::Tokens) -> Macro::Tokens {\n";
  for (const auto &entry : macros) {
    // The module is imported above, so the macro is named through it.
    std::string leaf = entry.second.Module;
    const size_t at = leaf.rfind("::");
    if (at != std::string::npos)
      leaf = leaf.substr(at + 2);
    src += "    if name == \"" + entry.first + "\" { return " + leaf + "::" +
           entry.first + "(input) }\n";
  }
  src += "    Macro::unknown(name)\n}\n\n";
  src += "fn main() -> i64 { Macro::serve(dispatch) }\n";
  return src;
}

} // namespace rune
