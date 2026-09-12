# Making a package

`rune new <name> --lib` lays one out. What it produces is the whole convention: a manifest, a `src/` whose `lib.rune` is the package's module, and room for tests and documentation.

```sh
$ rune new geometry --lib
$ tree geometry
geometry
├── Rune.toml
├── src
│   └── lib.rune        the package module: `import geometry` reaches this
├── tests               one program per file; `rune test` runs them
└── docs                prose; `rune doc` renders it beside the API
```

The manifest says what the package is. The first three fields are what a registry shows and what `rune search` matches against; `version` is what everything else keys on.

```sh
[package]
name = "geometry"
version = "0.3.0"
edition = "2025"
description = "Points and vectors on the plane: distances, dot products, and the arithmetic between them"
authors = ["The Rune examples"]
license = "MIT"

[build]
safety = "full"

[dependencies]
```

What a package offers is what it marks `pub`. A `///` comment above a public declaration is its documentation, and `docs/index.md` is the page a reader starts at — both end up in `rune doc`'s output and travel with the package into a registry.

**A library module**

```rune
// src/lib.rune — geometry, the plane and what can be done on it.

import std::io
import std::math

/// A position.
pub struct Point { pub x: f64, pub y: f64 }

/// A displacement: what one point is from another.
pub struct Vector { pub dx: f64, pub dy: f64 }

pub fn point(x: f64, y: f64) -> Point { Point { x: x, y: y } }

/// The straight-line distance between two points.
pub fn distance(a: Point, b: Point) -> f64 { (b - a).length() }

extend Vector {
    pub fn length(&self) -> f64 { math::lengthOf(self.dx, self.dy) }
}

/// `b - a` is the vector from `a` to `b`.
bind operator::sub to Point {
    fn sub(&self, other: Point) -> Vector { Vector { dx: self.x - other.x, dy: self.y - other.y } }
}

bind io::Display to Point {
    fn display(&self) -> String { "(" + self.x.$str() + ", " + self.y.$str() + ")" }
}
```

A test is an ordinary program under `tests/`. It imports the package by name, exactly as a user would, and reports through `std::testing`; `rune test` builds and runs every file there and reads the exit status.

```sh
// tests/basics.rune
import std::testing
import geometry

fn main() -> i64 {
    let a = geometry::point(0.0, 0.0)
    let b = geometry::point(3.0, 4.0)
    testing::equal("distance", geometry::distance(a, b), 5.0)
    testing::summary()
}
```

```sh
$ rune test
○ Compiling geometry v0.3.0 (library)
○ Compiling test basics
  ✓ distance
  1 passed
● Test result: 1 file(s) passed, 0 failed
$ rune doc --open
```
