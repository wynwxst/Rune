#include "Jobs.h"

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <thread>

namespace rune::pm {

namespace {

unsigned gJobLimit = 0;
std::mutex gOutputMutex;

} // namespace

unsigned jobLimit() {
  if (gJobLimit)
    return gJobLimit;
  unsigned hw = std::thread::hardware_concurrency();
  return hw ? hw : 1u;
}

void setJobLimit(unsigned n) { gJobLimit = n ? n : 1u; }

unsigned sharePerJob() {
  unsigned hw = std::thread::hardware_concurrency();
  if (!hw)
    return 1;
  unsigned share = hw / jobLimit();
  return share ? share : 1u;
}

void writeSerialized(const std::string &text) {
  std::lock_guard<std::mutex> lock(gOutputMutex);
  std::cerr << text;
  std::cerr.flush();
}

int runCaptured(const std::string &cmd, std::string &output) {
  output.clear();
  // `2>&1` so a diagnostic and whatever the step printed before it stay in the
  // order the step wrote them.
  std::string piped = "( " + cmd + " ) 2>&1";
  FILE *pipe = popen(piped.c_str(), "r");
  if (!pipe)
    return 127;
  char buffer[4096];
  while (size_t n = fread(buffer, 1, sizeof buffer, pipe))
    output.append(buffer, n);
  int rc = pclose(pipe);
  if (rc == -1)
    return 127;
  return (rc & 0x7F) ? 128 + (rc & 0x7F) : ((rc >> 8) & 0xFF);
}

bool runGraph(std::vector<Job> &jobs) {
  if (jobs.empty())
    return true;

  // How many of a job's dependencies are still outstanding, and who is
  // waiting on each job. Counting down rather than rescanning means a job
  // becomes ready the moment its last dependency finishes.
  std::vector<size_t> remaining(jobs.size());
  std::vector<std::vector<size_t>> dependents(jobs.size());
  for (size_t i = 0; i < jobs.size(); ++i) {
    remaining[i] = jobs[i].DependsOn.size();
    for (size_t d : jobs[i].DependsOn)
      dependents[d].push_back(i);
  }

  std::mutex mutex;
  std::condition_variable ready;
  std::vector<size_t> queue;
  size_t finished = 0;
  bool failed = false;

  for (size_t i = 0; i < jobs.size(); ++i)
    if (remaining[i] == 0)
      queue.push_back(i);

  // A cycle would leave every job waiting on another, and nothing ready to
  // start. The package graph is checked for cycles before it gets here; this
  // is the safety net that turns a mistake into an error rather than a hang.
  if (queue.empty())
    return false;

  const unsigned threads = static_cast<unsigned>(
      std::min<size_t>(jobLimit(), jobs.size()));

  auto worker = [&] {
    for (;;) {
      size_t index;
      {
        std::unique_lock<std::mutex> lock(mutex);
        ready.wait(lock, [&] {
          return !queue.empty() || finished == jobs.size() || failed;
        });
        // Once something has failed, nothing new starts: a step compiled
        // against a library that did not build has nothing useful to say.
        if (queue.empty() || failed)
          return;
        index = queue.back();
        queue.pop_back();
      }

      const bool ok = jobs[index].Run();

      {
        std::lock_guard<std::mutex> lock(mutex);
        ++finished;
        if (!ok)
          failed = true;
        else
          for (size_t d : dependents[index])
            if (--remaining[d] == 0)
              queue.push_back(d);
        // Everyone wakes: a worker may now have work, and the ones that do
        // not need to notice that the build is over.
        ready.notify_all();
      }
    }
  };

  std::vector<std::thread> workers;
  workers.reserve(threads - 1);
  for (unsigned t = 1; t < threads; ++t)
    workers.emplace_back(worker);
  worker();
  for (std::thread &w : workers)
    w.join();

  std::lock_guard<std::mutex> lock(mutex);
  return !failed && finished == jobs.size();
}

} // namespace rune::pm
