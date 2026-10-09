//===- runec_main.cpp - Rune compiler entry point ---------------*- C++ -*-===//

#include "rune/Driver.h"

int main(int argc, char **argv) {
  // The process ends with the compile; what it built is the operating
  // system's to reclaim.
  rune::setExitWithoutTeardown(true);
  return rune::runCompilerMain(argc, argv);
}
