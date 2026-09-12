//===- Ownership.h - Borrow and ownership checking -------------*- C++ -*-===//
//
// A function-local pass that runs after a body has been type-checked, at
// `--safety full`. It answers three questions about the body it is given:
//
//   * Does a borrow outlive what it borrows from?
//   * Do two borrows of the same place overlap when one of them can write?
//   * Does a value that owns something a reference count cannot see get
//     copied out of a place that still holds it?
//
// and one more that nothing reports, because its answer is an optimisation
// rather than a diagnostic:
//
//   * Which locals never leave the scope that declared them?
//
// A local that never escapes and is initialised by a construction nothing
// else refers to needs no reference counting at all: the allocation's own
// count is the binding's, and the scope hands it back on the way out. That is
// what `VarDecl::NoEscape` records, and what the code generator reads.
//
// The pass is deliberately conservative in both directions. Anything it
// cannot follow — a raw pointer, a borrow taken inside a closure, a place
// reached through a call — is treated as escaping, so the elision is only
// ever applied where the whole story is visible in one function body.
//
//===----------------------------------------------------------------------===//
#ifndef RUNE_OWNERSHIP_H
#define RUNE_OWNERSHIP_H

#include "rune/AST.h"
#include "rune/Diagnostics.h"
#include "rune/Driver.h"

namespace rune {

/// Checks `fn`'s body and records what it learned on the declarations inside
/// it. `safety` decides whether a finding is an error or a warning: at
/// `--safety full` a broken borrow stops the build, below it the program is
/// taken at its word and only told.
void checkOwnership(FunctionDecl *fn, DiagnosticEngine &diags,
                    SafetyLevel safety);

} // namespace rune

#endif
