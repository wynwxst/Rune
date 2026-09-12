//===- ASTWalk.h - Visiting the children of a node --------------*- C++ -*-===//
#ifndef RUNE_ASTWALK_H
#define RUNE_ASTWALK_H

#include "rune/AST.h"

#include <functional>

namespace rune {

using NodeVisitor = std::function<void(const Node *)>;

/// Calls `visit` once for each direct sub-node of `n` — one level, no
/// recursion, so the caller decides how deep to go and what to do on the way.
///
/// Declarations nested inside a body are not visited: each is checked as a
/// declaration of its own, and descending into one here would attribute its
/// contents to the body that happens to contain it.
void forEachChild(const Node *n, const NodeVisitor &visit);

} // namespace rune

#endif
