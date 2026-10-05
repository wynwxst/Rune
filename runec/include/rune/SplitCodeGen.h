//===- SplitCodeGen.h - Building one module's machine code in pieces -*- C++ -*-===//
//
// The back end is most of what a large compile spends, and it works one
// function at a time: nothing it does to one function looks at another. So a
// finished module can be cut into pieces and each piece built on a core of
// its own, the way Rust's codegen units are.
//
// The cut is made once, on the module the optimiser has finished with:
//
//   * every function with a body is given a home piece, balanced by size,
//     with the members of one COMDAT kept together;
//   * a global variable lives in the piece of the first function that uses
//     it, so a string constant travels with the code that reads it;
//   * a local symbol that another piece refers to becomes a hidden external
//     one — still invisible outside the program, but now something the
//     linker can join up.
//
// The module is then written as bitcode once. Each piece reads it lazily into
// a context of its own (an `LLVMContext` belongs to one thread), loads only
// the bodies that live there and turns everything else into a declaration.
//
//===----------------------------------------------------------------------===//
#ifndef RUNE_SPLITCODEGEN_H
#define RUNE_SPLITCODEGEN_H

#include <llvm/ADT/SmallString.h>

#include <memory>
#include <string>
#include <unordered_map>

namespace llvm {
class LLVMContext;
class Module;
} // namespace llvm

namespace rune {

/// How `splitModule` cut a module: the bitcode every piece is read from, and
/// which piece each definition lives in.
struct CodegenSplit {
  unsigned Units = 1;
  llvm::SmallString<0> Bitcode;
  /// A definition's name, and the piece it is defined in.
  std::unordered_map<std::string, unsigned> Home;
};

/// Cuts `m` into at most `units` pieces. Returns false when the module has
/// something that cannot be cut — an alias, module-level assembly — or too
/// little to be worth it, and leaves `m` as it was.
bool splitModule(llvm::Module &m, unsigned units, CodegenSplit &out);

/// Piece `index` of `split`, read into `ctx`: its own definitions, and a
/// declaration of everything else. Null, with `err` set, if the bitcode
/// could not be read.
std::unique_ptr<llvm::Module> loadPiece(const CodegenSplit &split,
                                        unsigned index,
                                        llvm::LLVMContext &ctx,
                                        std::string &err);

} // namespace rune

#endif
