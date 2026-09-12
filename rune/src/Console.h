//===- Console.h - Status lines, in the compiler's style --------*- C++ -*-===//
//
// Shared by the build front end and the package registry, so a line printed
// by either looks like one the compiler printed: a coloured dot, a verb, and
// the detail. Everything goes through `writeSerialized`, so lines from
// several threads never interleave.
//
//===----------------------------------------------------------------------===//
#ifndef RUNE_PM_CONSOLE_H
#define RUNE_PM_CONSOLE_H

#include <filesystem>
#include <string>

namespace rune::pm {

/// Whether output carries colour codes. Set once from `isatty` and `--no-color`.
extern bool gColor;

const char *c(const char *code);
void status(const std::string &verb, const std::string &detail);
void okLine(const std::string &text);
void failLine(const std::string &text);
void warnLine(const std::string &text);
void note(const std::string &text);
/// A plain line, serialised with the rest.
void plain(const std::string &text);

/// `s` as one shell word.
std::string quote(const std::string &s);

/// `$RUNE_HOME`, or `~/.rune`: where the toolchain keeps what it caches and
/// installs.
std::filesystem::path runeHome();

} // namespace rune::pm

#endif
