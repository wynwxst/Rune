//===- Jobs.h - Running independent build steps at the same time -*- C++ -*-===//
//
// Two things have to hold for a build to run several compiles at once and
// still be worth reading afterwards.
//
// The first is that a step only starts once everything it depends on has
// finished. That is what `runGraph` is for: it is handed steps and, for each,
// which other steps must come first, and it keeps as many running as there
// are jobs while never breaking that order.
//
// The second is that output stays legible. A compiler diagnostic is a dozen
// lines with carets lined up under source text, and two of them woven
// together is worse than useless — so a step's output is captured and printed
// in one piece when it is done, rather than as it appears.
//
//===----------------------------------------------------------------------===//
#ifndef RUNE_PM_JOBS_H
#define RUNE_PM_JOBS_H

#include <functional>
#include <string>
#include <vector>

namespace rune::pm {

/// How many steps may run at once. Set from `-j`, and otherwise from what the
/// machine reports; never zero.
unsigned jobLimit();
void setJobLimit(unsigned n);

/// How many threads one step may use for itself, so that all of them together
/// come to about one machine's worth. Never zero: a step always gets at least
/// the thread it is running on.
unsigned sharePerJob();

/// Runs `cmd`, returning its exit code and collecting everything it wrote to
/// either stream into `output`.
///
/// Capturing rather than inheriting is what lets a step's diagnostics be
/// printed in one piece. A child whose output is a pipe will not colour it on
/// its own, so a caller that wants colour has to ask the child for it.
int runCaptured(const std::string &cmd, std::string &output);

/// Prints `text` with nothing else able to print between its lines.
void writeSerialized(const std::string &text);

/// One step of a build.
struct Job {
  /// Indices, into the same vector this job is in, of steps that must finish
  /// before this one starts.
  std::vector<size_t> DependsOn;
  /// What the step does. Returns false to fail the build.
  std::function<bool()> Run;
};

/// Runs every job, respecting `DependsOn`, on up to `jobLimit()` threads.
///
/// Returns false if any job failed. A failure stops new jobs from starting —
/// there is no point compiling against a library that did not build — but
/// lets the ones already running finish, so their diagnostics are not lost
/// halfway through.
bool runGraph(std::vector<Job> &jobs);

} // namespace rune::pm

#endif
