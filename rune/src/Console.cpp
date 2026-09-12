#include "Console.h"
#include "Jobs.h"

#include <cstdlib>

namespace rune::pm {

bool gColor = true;

const char *c(const char *code) { return gColor ? code : ""; }

// Each of these is built as one string and written in one go: a step that
// fails does so on whichever thread was running it, and a half-written line
// with another thread's line inside it helps nobody.
void status(const std::string &verb, const std::string &detail) {
  writeSerialized(std::string(c("\x1b[32m")) + "○" + c("\x1b[0m") + " " +
                  c("\x1b[1m") + verb + c("\x1b[0m") + " " + detail + "\n");
}
void okLine(const std::string &text) {
  writeSerialized(std::string(c("\x1b[32m")) + "●" + c("\x1b[0m") + " " +
                  text + "\n");
}
void failLine(const std::string &text) {
  writeSerialized(std::string(c("\x1b[1;31m")) + "●" + c("\x1b[0m") + " " +
                  text + "\n");
}
void warnLine(const std::string &text) {
  writeSerialized(std::string(c("\x1b[1;33m")) + "●" + c("\x1b[0m") + " " +
                  text + "\n");
}
void note(const std::string &text) {
  writeSerialized(std::string("  ") + c("\x1b[2m") + "─" + c("\x1b[0m") +
                  "  " + c("\x1b[1m") + "note:" + c("\x1b[0m") + " " + text +
                  "\n");
}
void plain(const std::string &text) { writeSerialized(text + "\n"); }

std::string quote(const std::string &s) {
  std::string out = "'";
  for (char ch : s) {
    if (ch == '\'')
      out += "'\\''";
    else
      out += ch;
  }
  return out + "'";
}

std::filesystem::path runeHome() {
  if (const char *h = std::getenv("RUNE_HOME"))
    return std::filesystem::path(h);
  if (const char *h = std::getenv("HOME"))
    return std::filesystem::path(h) / ".rune";
  return std::filesystem::path(".rune");
}

} // namespace rune::pm
