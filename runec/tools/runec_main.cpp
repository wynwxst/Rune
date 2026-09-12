//===- runec_main.cpp - Rune compiler entry point ---------------*- C++ -*-===//

#include "rune/Driver.h"

int main(int argc, char **argv) {
  return rune::runCompilerMain(argc, argv);
}
