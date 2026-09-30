"""Source of truth for the Rune HTML reference.

Every `S(...)` sample is handed to the compiler by build.py:

    mode="run"    a complete program; its real stdout is shown beneath it
    mode="decls"  declarations only; a trivial `main` is appended to compile it
    mode="diag"   expected to be rejected; the real diagnostic is shown
    mode="frag"   not Rune (grammar, shell), so not compiled
"""


def P(text):
    return {"kind": "prose", "text": text}


def H(text):
    return {"kind": "heading", "text": text}


def S(code, mode="decls", title=None, safety="", memory=""):
    return {"kind": "sample", "code": code, "mode": mode, "title": title,
            "safety": safety, "memory": memory}


def T(headers, rows, caption=None):
    return {"kind": "table", "headers": headers, "rows": rows, "caption": caption}


def N(text, label="Note", tone="note"):
    return {"kind": "note", "text": text, "label": label, "tone": tone}


def G(text):
    return {"kind": "grammar", "text": text}


def SH(text):
    return {"kind": "shell", "text": text}


def Sec(id, kicker, title, blurb, items, keywords=()):
    return {
        "id": id,
        "kicker": kicker,
        "title": title,
        "blurb": blurb,
        "items": items,
        "keywords": list(keywords),
    }


SECTIONS = []

# ===========================================================================
# 1. Orientation
# ===========================================================================
SECTIONS.append(Sec(
    "start", "orientation", "Getting started",
    "Rune is a statically typed, compiled language with automatic reference "
    "counting and safety you can dial down. It reads like Swift and Rust, "
    "compiles through LLVM, and has no runtime beyond a small C library.",
    keywords=["install", "build", "hello", "cmake", "llvm", "first program"],
    items=[
        H("Building the toolchain"),
        P("The repository builds two binaries: `runec`, the compiler, and "
          "`rune`, the package manager. You need CMake 3.20 or newer, a C++20 "
          "compiler, and LLVM 17+ with development headers."),
        SH("""cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure

export PATH="$PWD/build/bin:$PATH\""""),
        P("If LLVM lives somewhere unusual, point CMake at it directly with "
          "`-DLLVM_DIR=$(llvm-config --cmakedir)`."),
        N("The build type matters more than usual, because `runec` links LLVM "
          "statically. A `Debug` build of the compiler is around 180 MB and "
          "spends about 20 ms getting to `main` before any work starts \u2014 "
          "which every compile in every build pays. `RelWithDebInfo` is the "
          "default when nothing says otherwise, and roughly halves the time a "
          "compile takes; use `Debug` only when stepping through the compiler "
          "itself.", label="Build the toolchain optimised"),

        H("Your first program"),
        P("A program is a module with a `main`. The result becomes the process "
          "exit status."),
        S("""import std::io

fn main() -> i64 {
    io::println("Hello from Rune!")
    0
}""", mode="run", title="hello.rune"),
        SH("""runec -o hello hello.rune && ./hello"""),

        H("Using the package manager"),
        P("`rune` handles layout, dependencies and linking. `rune new` "
          "scaffolds a package; `rune run` builds and runs it."),
        SH("""rune new hello        # or: rune new mylib --lib
cd hello
rune run
rune test
rune build --release"""),
        T(["Path", "Meaning"],
          [["`Rune.toml`", "the package manifest"],
           ["`src/main.rune`", "binary root, becomes `target/<profile>/<name>`"],
           ["`src/lib.rune`", "library root, becomes `target/<profile>/<name>.rul`"],
           ["`src/other.rune`", "a submodule, importable as `<name>::other`"],
           ["`tests/*.rune`", "one test program per file, run by `rune test`"],
           ["`target/`", "build output; safe to delete"]],
          caption="Package layout"),

        H("How to read this reference"),
        P("Every sample on this page was handed to `runec` when the page was "
          "generated, and nothing shown below one is transcribed by hand. Some "
          "are complete programs that were compiled, linked and run; some are "
          "declarations, type-checked with a trivial `main` appended; some are "
          "meant to be rejected, and the diagnostic is the compiler's own, "
          "colours and all. Grammar and shell listings are the only text here "
          "that is not Rune."),
        P("What a sample produced is folded away beneath it — a line count you "
          "can click open. Output and diagnostics read differently: a "
          "diagnostic's handle is red, and the block keeps the compiler's own "
          "layout."),
        T(["On the page", "Is"],
          [["a caption above the code", "what the sample is showing"],
           ["`COPY`, top right", "copies the source, without the line numbers"],
           ["`▸ 6 lines`", "what the program printed — click to open"],
           ["`▸ 12 lines` in red", "why the compiler rejected it"],
           ["`←` and `→`", "the previous and next category"],
           ["`/`", "jumps to the filter box"]],
          caption="One category to a page. The rail on the left moves between "
                  "them; the list on the right moves within one."),
    ]))

# ===========================================================================
# 2. Lexical structure
# ===========================================================================
SECTIONS.append(Sec(
    "lexical", "tokens", "Lexical structure",
    "How source text becomes tokens: where statements end, what a literal can "
    "look like, and which words are reserved.",
    keywords=["comment", "semicolon", "newline", "literal", "escape", "keyword",
              "identifier", "raw string", "unicode"],
    items=[
        H("Statements and line breaks"),
        P("Semicolons are optional. A line break ends a statement when the "
          "token before it *could* end one — an identifier, a literal, a "
          "closing bracket, or a keyword like `return`. Otherwise the "
          "statement carries on to the next line."),
        S("""import std::io

fn main() -> i64 {
    // `+` cannot end a statement, so this is one expression.
    let total = 1 +
                2 +
                3

    // A line starting with `.` continues the previous expression.
    let text = "chained"
        .$repeat(2)

    // An explicit `;` ends a statement wherever you want it to.
    let a = 1; let b = 2

    io::println(total.$str() + " " + text + " " + (a + b).$str())
    0
}""", mode="run", title="Three ways a statement can end"),
        N("Inside `(` … `)` and `[` … `]`, line breaks are never terminators, "
          "so argument lists and array literals can span as many lines as you "
          "like.", label="Grouping"),
        P("The `;` also decides whether a trailing expression is the block's "
          "value or a discarded statement — see **Blocks are expressions**."),

        H("Comments"),
        S("""// A line comment runs to the end of the line.

/* A block comment,
   which /* nests */ correctly. */

/// Three slashes is still just a line comment; it reads as documentation
/// but the compiler treats it the same.
pub fn documented() -> i64 { 1 }"""),

        H("Integer literals"),
        P("Decimal, hexadecimal, binary and octal, with `_` allowed anywhere as "
          "a separator. A suffix pins the type; without one the literal takes "
          "the type its context wants, defaulting to `i64`."),
        S("""import std::io

fn main() -> i64 {
    let decimal = 1_000_000
    let hex = 0xFF
    let binary = 0b1010_1010
    let octal = 0o755
    let sized: u8 = 200
    let suffixed = 42i32
    let unsigned = 255u8
    let wide = 9_223_372_036_854_775_807

    io::println(decimal)
    io::println(hex)
    io::println(binary)
    io::println(octal)
    io::println(sized)
    io::println(suffixed)
    io::println(unsigned)
    io::println(wide)
    0
}""", mode="run", title="Every integer form"),
        T(["Suffix", "Type"],
          [["`i8` `i16` `i32` `i64`", "signed integers of that width"],
           ["`u8` `u16` `u32` `u64`", "unsigned integers of that width"],
           ["`isize` `usize`", "pointer-sized: 8 bytes on a 64-bit target, 4 "
            "on a 32-bit one"],
           ["`f32` `f64`", "makes an integer literal a float"]]),

        H("Float literals"),
        S("""import std::io

fn main() -> i64 {
    let plain = 1.5
    let exponent = 1e-3
    let both = 6.022e23
    let single: f32 = 0.25
    let suffixed = 2.5f32
    let fromInt = 3f64

    io::println(plain)
    io::println(exponent)
    io::println(both)
    io::println(single)
    io::println(suffixed)
    io::println(fromInt)
    0
}""", mode="run", title="Every float form"),
        N("A `.` only begins a fraction when a digit follows it, which is what "
          "keeps `1..5` a range and `pair.0.1` two field accesses.",
          label="Why `1..5` works"),

        H("Characters and escapes"),
        P("A `Character` is one Unicode scalar, written in single quotes."),
        S("""import std::io

fn main() -> i64 {
    let letter = 'R'
    let newline = '\\n'
    let tab = '\\t'
    let quote = '\\''
    let backslash = '\\\\'
    let nul = '\\0'
    let escape = '\\e'
    let hexByte = '\\x41'
    let greek = 'λ'
    let scalar = '\\u{1F600}'

    io::println(letter)
    io::println(hexByte)
    io::println(greek)
    io::println(scalar)
    io::println("tab between:[" + tab.$str() + "]")
    io::println(nul.$str().$length())
    io::println(quote.$str() + backslash.$str() + newline.$str().$length().$str() +
                escape.$str().$length().$str())
    0
}""", mode="run", title="Character literals and escapes"),
        T(["Escape", "Meaning"],
          [["`\\n` `\\t` `\\r`", "newline, tab, carriage return"],
           ["`\\0`", "the NUL scalar"],
           ["`\\\\` `\\\"` `\\'`", "backslash, double quote, single quote"],
           ["`\\e`", "escape (0x1B), handy for terminal output"],
           ["`\\xNN`", "one byte from exactly two hex digits"],
           ["`\\u{...}`", "any Unicode scalar up to `\\u{10FFFF}`"]]),

        H("String literals"),
        P("A `\"...\"` literal is a `String` — owned, reference counted, UTF-8 "
          "— unless the surrounding context wants a `CString`, in which case it "
          "becomes a borrowed NUL-terminated byte pointer instead. Raw strings "
          "take no escapes at all."),
        S("""import std::io

fn main() -> i64 {
    let plain = "ordinary text"
    let escaped = "tab\\there, quote \\" here"
    let unicode = "check \\u{2713}"
    let raw = r"C:\\not\\an\\escape"
    let rawQuotes = r#"he said "hello""#

    io::println(plain)
    io::println(escaped)
    io::println(unicode)
    io::println(raw)
    io::println(rawQuotes)
    0
}""", mode="run", title="String and raw-string literals"),
        N("A string literal cannot span lines. Build a multi-line value by "
          "concatenating with `+`, or embed `\\n`.", label="One line only",
          tone="warn"),

        H("`.` and `::`"),
        P("Two separators, and the rule is about *what* is on the left, never "
          "about what is on the right. `::` walks a path through things the "
          "compiler knows by name — modules, types, marks, enums. `.` reaches "
          "into a value you have in your hand."),
        T(["Written", "Left side", "Means"],
          [["`std::io`", "a module", "a module inside another"],
           ["`io::println(x)`", "a module", "a function in it"],
           ["`math::PI`", "a module", "a global in it"],
           ["`Shape::Circle`", "an enum", "one of its variants"],
           ["`Sheep::new(\"d\")`", "a type", "a method with no `self`"],
           ["`Animal::new(\"d\")`", "a mark", "a static requirement, for the "
            "type in context"],
           ["`Pair<i64, String>`", "a type", "generic arguments, no separator"],
           ["`total::<i64>(xs)`", "a function", "generic arguments at a call"],
           ["—", "—", "—"],
           ["`point.x`", "a value", "a field"],
           ["`point.length()`", "a value", "a method"],
           ["`text.$length()`", "a value", "a method the *compiler* provides"],
           ["`pair.0`", "a value", "a tuple element"],
           ["`value::Mark.name()`", "a value, then a mark", "the method that "
            "mark binds for this type"]],
          caption="The last row is the only place the two meet: `::` picks the "
                  "mark, `.` then reaches the method through it."),
        S("""import std::io
import std::math

enum Shape { Circle(f64), Square(f64) }

struct Point { x: f64, y: f64 }

extend Point {
    pub fn distance(&self) -> f64 {
        math::squareRoot(self.x * self.x + self.y * self.y)
    }
}

fn main() -> i64 {
    // `::` through modules and types.
    io::println(math::PI.$str())
    let s = Shape::Circle(2.0)

    // `.` into a value.
    let p = Point { x: 3.0, y: 4.0 }
    io::println(p.x.$str())
    io::println(p.distance().$str())

    // And `$` for what the compiler provides, on any value.
    io::println("hello".$length().$str())
    0
}""", mode="run", title="Both, side by side"),
        N("A path is resolved left to right, so `a::b::c` needs every step to "
          "name something. If the first step is a local variable, you are "
          "writing the mark-qualified form and the rest must name a mark.",
          label="Resolution order"),

        H("Reserved words"),
        P("These 39 words are keywords and cannot be used as identifiers."),
        T(["Group", "Words"],
          [["Declarations", "`fn` `struct` `enum` `class` `mark` `bind` `to` "
            "`extend` `import` `extern` `type` `pub` `global`"],
           ["Bindings", "`let` `var` `mut`"],
           ["Control flow", "`if` `elif` `else` `while` `loop` `for` `in` "
            "`match` `return` `break` `continue` `defer` `async` `await`"],
           ["Types and values", "`self` `Self` `super` `dyn` `weak` `true` "
            "`false` `nil`"],
           ["Other", "`as` `is` `where` `unsafe` `operator`"]]),
        P("`Option`, `Result`, `Some`, `None`, `Ok` and `Err` are not keywords "
          "— they are ordinary declarations the compiler happens to know by "
          "name, and a module may shadow them."),
    ]))

# ===========================================================================
# 3. Bindings and scope
# ===========================================================================
SECTIONS.append(Sec(
    "bindings", "values", "Bindings and scope",
    "Names are immutable unless you say otherwise. Blocks are expressions, so "
    "a binding can be initialised by a whole computation.",
    keywords=["let", "var", "mut", "global", "shadow", "block", "immutable",
              "destructuring", "uninitialised"],
    items=[
        H("Declaring a binding"),
        P("There are four spellings, and they differ only in what they say out "
          "loud. A bare `name = value` declares a binding when nothing of that "
          "name is in scope; `let` says the same thing explicitly; `var` and "
          "`mut` are the two ways to ask for mutation."),
        S("""import std::io

fn main() -> i64 {
    inferred = 7             // declares an immutable binding
    annotated: i64 = 7       // with the type written out
    let explicit = 7         // the same, said explicitly
    let typed: i64 = 7

    var counter = 0          // mutable
    mut also = 0             // `mut` is a synonym for `var`
    counter += 1
    also += 2

    var buffer: [4:u8]       // mutable and uninitialised

    io::println(inferred + annotated + explicit + typed)
    io::println(counter + also)
    io::println(buffer.$length())
    0
}""", mode="run", title="Every binding form"),

        H("Immutability is checked"),
        P("Assigning to a binding that was never declared `var` is an error, "
          "and the diagnostic points at both the assignment and the "
          "declaration it conflicts with."),
        S("""fn main() -> i64 {
    let total = 0
    total = 1
    total
}""", mode="diag", title="Reassigning an immutable binding"),

        H("Destructuring"),
        P("A binding's left side is a pattern, so a tuple can be taken apart "
          "on the way in."),
        S("""import std::io

fn minMax(values: [4:i64]) -> (i64, i64) {
    var low = values[0]
    var high = values[0]
    for v in values {
        if v < low { low = v }
        if v > high { high = v }
    }
    (low, high)
}

fn main() -> i64 {
    let (a, b) = (10, 20)
    let (low, high) = minMax([3, 9, 1, 7])
    io::println(a.$str() + " " + b.$str())
    io::println(low.$str() + ".." + high.$str())
    0
}""", mode="run", title="Tuple destructuring"),

        H("Scope and shadowing"),
        S("""import std::io

fn main() -> i64 {
    let raw = "  42  "
    // Each `let` makes a new binding; the old one is untouched, and the type
    // may change on the way.
    let raw = raw.$substring(2, 4)
    let raw = raw.$toInt().or(0)
    io::println(raw.$str())
    0
}""", mode="run", title="Shadowing changes the type as it goes"),
        P("Bindings belong to the block that declares them. A nested block may "
          "reuse a name without disturbing the outer one."),
        S("""import std::io

fn main() -> i64 {
    let value = "outer"
    {
        let value = "inner"
        io::println(value)
    }
    io::println(value)
    0
}""", mode="run", title="An inner name shadows an outer one"),

        H("Globals"),
        P("`global` declares a binding at module scope. It needs either a type "
          "or an initialiser, and initialisers run once, before `main`, in "
          "declaration order."),
        S("""import std::io

global var counter: i64 = 0
global GREETING: String = "set up before main runs"
global LIMIT: i64 = 4 * 4

fn bump() -> i64 {
    counter += 1
    counter
}

fn main() -> i64 {
    bump()
    bump()
    io::println(GREETING)
    io::println(counter)
    io::println(LIMIT)
    0
}""", mode="run", title="Module-level state"),
        P("Inside a function, `global name: T` refers to a module-level binding "
          "rather than declaring a local one."),

        H("Blocks are expressions"),
        P("A block's value is its last expression, provided that expression has "
          "no semicolon. The same rule gives functions their result."),
        S("""import std::io

fn main() -> i64 {
    let computed = {
        let base = 2 + 2
        base * base          // no semicolon: this is the block's value
    }

    let discarded = {
        let base = 2 + 2
        base * base;         // semicolon: the value is dropped
        99
    }

    io::println(computed)
    io::println(discarded)
    0
}""", mode="run", title="The semicolon decides"),
        N("A function with no `->` returns `()`. A trailing expression in such "
          "a function is evaluated and its value discarded.",
          label="Void functions"),
    ]))

# ===========================================================================
# 4. Types
# ===========================================================================
SECTIONS.append(Sec(
    "types", "the type system", "Types",
    "Rune has no implicit narrowing and no truthiness. Every type below is "
    "written the same way in a declaration, a parameter list and a cast.",
    keywords=["i64", "f64", "bool", "String", "CString", "Character", "array",
              "slice", "tuple", "pointer", "borrow", "function type", "alias",
              "conversion", "cast", "widening", "Never", "some"],
    items=[
        H("The full set"),
        T(["Category", "Written", "Notes"],
          [["Signed integers", "`i8` `i16` `i32` `i64` `isize`",
            "`int` is an alias for `i64`"],
           ["Unsigned integers", "`u8` `u16` `u32` `u64` `usize`",
            "`uint` aliases `u64`; `byte` and `Byte` both alias `u8`"],
           ["Floating point", "`f32` `f64`",
            "`float` aliases `f32`, `double` aliases `f64`"],
           ["Boolean", "`bool`", "no implicit conversion to or from integers"],
           ["Character", "`Character`", "exactly one Unicode scalar"],
           ["Foreign text", "`CString`", "a borrowed NUL-terminated byte pointer"],
           ["Text", "`String`", "owned, reference counted, UTF-8"],
           ["Array", "`[5:i64]`", "fixed length, a constant expression"],
           ["Slice", "`[i64]`", "a pointer and a length"],
           ["Tuple", "`(i64, String)`", "`()` is the unit type"],
           ["Optional", "`i64?`", "sugar for `Option<i64>`"],
           ["Borrow", "`&T` `&var T`", "checked, non-owning"],
           ["Raw pointer", "`*T` `*var T`", "unchecked; every use is unsafe"],
           ["Function", "`@function(i64) -> bool`", "see below for both spellings"],
           ["Nominal", "`struct` `enum` `class` `mark`", "declared types"],
           ["Mark object", "`dyn Show`", "a value plus a dispatch table"],
           ["Opaque result", "`some Show`",
            "the body fixes the type; callers see only the mark"],
           ["Dynamic", "`Any`", "one value of any type, asked at run time what it is"],
           ["Never", "`Never`", "the type of an expression that does not return"]],
          caption="Every type Rune has"),
        S("""import std::io

struct Pair { left: i64, right: i64 }
enum Colour { Red, Green }
class Handle { pub id: i64
    fn init(self, id: i64) { self.id = id } }
mark Named { fn name(&self) -> String }
bind Named to Pair { fn name(&self) -> String { "pair" } }

fn main() -> i64 {
    let signed: i64 = -5
    let unsigned: u32 = 5
    let real: f64 = 2.5
    let flag: bool = true
    let letter: Character = 'R'
    let foreign: CString = "for C"
    let text: String = "owned"
    let array: [3:i64] = [1, 2, 3]
    let slice: [i64] = array[0..2]
    let tuple: (i64, String) = (1, "one")
    let optional: i64? = 7
    let pair = Pair { left: 1, right: 2 }
    let borrow: &Pair = &pair
    let colour: Colour = Colour::Green
    let handle: Handle = Handle(9)
    let marked: dyn Named = pair

    io::println(signed.$str() + " " + unsigned.$str() + " " + real.$str())
    io::println(flag.$str() + " " + letter.$str() + " " + text)
    io::println(array.$length().$str() + " " + slice.$length().$str())
    io::println(tuple.1 + " " + optional.or(0).$str())
    io::println(borrow.left.$str() + " " + handle.id.$str())
    io::println(marked.name() + " " + (colour as i64).$str())
    io::println(foreign.$str())
    0
}""", mode="run", title="One of everything"),

        H("Function types"),
        P("A function type is written the way a `fn` is: the parameters in "
          "parentheses, then `->` and the result. Leave the arrow off and the "
          "function returns nothing, exactly as a `fn` with no `->` does — so "
          "`@function(i64)` takes an `i64` and produces `()`."),
        S("""import std::io

type Predicate = @function(i64) -> bool
type Combine = @function(i64, i64) -> i64
type Producer = @function() -> i64             // no parameters, returns i64
type Sink = @function(i64)                     // takes an i64, returns nothing

fn apply(f: Predicate, value: i64) -> bool { f(value) }
fn fold(f: Combine, a: i64, b: i64) -> i64 { f(a, b) }
fn produce(f: Producer) -> i64 { f() }
fn drain(f: Sink, value: i64) { f(value) }

fn main() -> i64 {
    let isPositive = ||(n: i64) -> bool { n > 0 }
    let add = ||(a: i64, b: i64) -> i64 { a + b }
    io::println(apply(isPositive, 3))
    io::println(fold(add, 20, 22))
    io::println(produce(||() -> i64 { 7 }))
    drain(||(n: i64) { io::println(n) }, 9)
    0
}""", mode="run", title="Function types"),

        H("Type aliases"),
        S("""import std::io

type Celsius = f64
type Reading = (Celsius, String)

fn describe(r: Reading) -> String { r.1 + " at " + r.0.$str() }

fn main() -> i64 {
    let now: Reading = (21.5, "kitchen")
    io::println(describe(now))
    0
}""", mode="run", title="A name for a shape you keep repeating"),
        P("`type` gives a name to any type. It is a pure alias: the two names "
          "are interchangeable everywhere."),
        S("""import std::result

type Pair = (i64, i64)
type Grid = [9:i64]
type Callback = @function(i64) -> bool
type Parsed = result::Result<i64, String>
type Maybe = i64?

fn first(p: Pair) -> i64 { p.0 }
fn size(g: Grid) -> i64 { g.$length() }"""),
        P("An alias may take parameters of its own, and then it stands for a "
          "different type at each use: `Row<i64>` is a slice of integers and "
          "`Row<String>` a slice of strings, exactly as if each had been "
          "written out."),
        S('''import std::io
import std::collections::vector

type Row<T> = [T]
type Pairing<A, B> = (A, B)
type Table<T> = vector::Vector<T>

fn total(values: Row<i64>) -> i64 {
    var t = 0
    for v in values { t += v }
    t
}

fn joined(words: Row<String>) -> String {
    var s = ""
    for w in words { s += w }
    s
}

fn main() -> i64 {
    let ns: [3:i64] = [1, 2, 3]
    let ws: [2:String] = ["a", "b"]
    io::println(total(ns).$str())
    io::println(joined(ws))

    let both: Pairing<i64, String> = (7, "seven")
    io::println(both.1 + " " + both.0.$str())

    // Built through the alias, as the type it stands for.
    var t = Table<i64>()
    t.push(9)
    io::println(t.at(0).or(0).$str())
    0
}''', mode="run", title="An alias with parameters of its own"),
        N("The arguments have to match what the alias declares: `Row` takes "
          "one, `Pairing` two. An alias that declares none takes none.",
          label="One for one"),

        H("`typeof`: the type an expression has"),
        P("`typeof(expr)` is whatever type the expression would have. The "
          "expression is checked and **never run** — it is there to be "
          "asked about, not evaluated — so `typeof(boom())` costs nothing "
          "and calls nothing."),
        S("""import std::io

struct Point { x: f64, y: f64 }

fn boom() -> i64 { io::println("never printed"); 0 }

fn main() -> i64 {
    let a = 7
    let b: typeof(a) = 9
    var p: typeof(Point { x: 0.0, y: 0.0 }) = Point { x: 3.0, y: 4.0 }
    let c: typeof(boom()) = 5

    io::println(b.$str())
    io::println(p.x.$str())
    io::println(c.$str())
    0
}""", mode="run", title="A type read off a value"),
        P("What it is really for is a macro that has to write a signature out "
          "of the values it was handed. Without it the caller spells every "
          "type a second time and keeps the two in step by hand; with it the "
          "signature is built from the call itself."),
        S("""macro send {
    ($fn: expr, $recv: expr $(, $item: expr)*) => {
        ($fn as @cfunction(*var u8 $(, typeof($item))*) -> *var u8)(
            $recv $(, $item)*)
    }
}

extern "C" {
    fn objc_msgSend(id: *var u8, ...) -> *var u8
}

struct Rect { x: f64, y: f64, w: f64, h: f64 }

@unsafe fn main() -> i64 {
    let obj = 0 as *var u8
    let r = Rect { x: 1.0, y: 2.0, w: 3.0, h: 4.0 }
    // Stands for a call through
    //   @cfunction(*var u8, Rect, u64, bool) -> *var u8
    let _ = send!(objc_msgSend, obj, r, 7u64, false)
    0
}""", mode="frag", title="A signature built from the arguments"),
        N("`typeof` is not a keyword. It is read this way only in a type "
          "position followed by `(`, so a function or a variable called "
          "`typeof` goes on meaning what it did.",
          label="Contextual, not reserved"),

        H("Conversions that happen on their own"),
        P("Widening that cannot lose information is implicit. Everything else "
          "needs `as`."),
        T(["From", "To", "When"],
          [["`i32`", "`i64`", "same signedness, not narrower"],
           ["`u16`", "`i32`", "unsigned to a strictly wider signed type"],
           ["`f32`", "`f64`", "float to a wider float"],
           ["`&var T`", "`&T`", "a mutable borrow where a shared one is wanted"],
           ["`[N:T]`", "`[T]`", "an array where a slice is wanted"],
           ["`Derived`", "`Base`", "a subclass where its base class is wanted"],
           ["`T`", "`T?`", "wrapped as `Option::Some(value)`"],
           ["`T`", "`dyn Mark`", "when `T` is bound to that mark"],
           ["`some Mark`", "`dyn Mark`", "the hidden type is bound, so it boxes"],
           ["`T`", "`Any`", "anything with a run-time representation"],
           ["`bool`", "any integer", "`false` is 0 and `true` is 1"],
           ["`T`", "`U`", "where the destination is written down and "
            "`bind T into U` says how"],
           ["`Never`", "anything", "the expression never produced a value"]],
          caption="Implicit conversions"),
        N("A boolean converts to a number because it is one of two values and "
          "every integer type has room for both — which is what lets a "
          "foreign `BOOL` parameter take `false` rather than a hand-written "
          "`NO: i8 = 0`. The reverse is not a conversion: which integers count "
          "as true is a question with no one answer, so `n != 0` is how the "
          "program says which one it means.", label="Booleans go one way"),
        S("""import std::io

fn wide(n: i64) -> i64 { n }
fn shared(values: [i64]) -> i64 { values.$length() }
fn optional(v: i64?) -> i64 { v.or(-1) }

fn main() -> i64 {
    let narrow: i32 = 7
    let single: f32 = 1.5
    let array: [3:i64] = [1, 2, 3]

    io::println(wide(narrow))              // i32 widens to i64
    io::println(shared(array))             // array decays to a slice
    io::println(optional(5))               // 5 wraps as Some(5)
    io::println((single as f64) + 0.25)    // f32 widens on its own too
    0
}""", mode="run", title="Widening in argument position"),

        H("`into`: conversions you write yourself"),
        P("`as` has fixed meanings — numbers, enums, pointers — and no binding "
          "can change them. A conversion between two of your own types is "
          "`into`, which dispatches through the built-in `As` mark. "
          "`bind Celsius into Fahrenheit` is the same as "
          "`bind As<Fahrenheit> to Celsius`: the binding reads the same way as "
          "the value, `c into Fahrenheit`. One source may convert into as many "
          "destinations as it likes."),
        S("""import std::io

struct Celsius { v: f64 }
struct Fahrenheit { v: f64 }
struct Kelvin { v: f64 }

bind Celsius into Fahrenheit {
    fn convert(&self) -> Fahrenheit { Fahrenheit { v: self.v * 1.8 + 32.0 } }
}
bind Celsius into Kelvin {
    fn convert(&self) -> Kelvin { Kelvin { v: self.v + 273.15 } }
}
bind Fahrenheit into Celsius {
    fn convert(&self) -> Celsius { Celsius { v: (self.v - 32.0) / 1.8 } }
}
bind Kelvin into Fahrenheit {
    fn convert(&self) -> Fahrenheit {
        Fahrenheit { v: (self.v - 273.15) * 1.8 + 32.0 }
    }
}

fn main() -> i64 {
    let c = Celsius { v: 100.0 }
    io::println((c into Fahrenheit).v.$str())
    io::println((c into Kelvin).v.$str())

    // They compose, left to right.
    io::println((c into Kelvin into Fahrenheit into Celsius).v.$str())
    0
}""", mode="run", title="One source, several destinations"),
        P("Because `As<Target>` is an ordinary generic mark, it works as a "
          "bound: a function can require that whatever it is given converts "
          "into the type it needs."),
        S("""import std::io

struct Celsius { v: f64 }
struct Kelvin { v: f64 }
struct Fahrenheit { v: f64 }

bind Celsius into Fahrenheit {
    fn convert(&self) -> Fahrenheit { Fahrenheit { v: self.v * 1.8 + 32.0 } }
}
bind Kelvin into Fahrenheit {
    fn convert(&self) -> Fahrenheit {
        Fahrenheit { v: (self.v - 273.15) * 1.8 + 32.0 }
    }
}

fn isWarm<T: As<Fahrenheit>>(v: T) -> bool { (v into Fahrenheit).v > 80.0 }

fn main() -> i64 {
    io::println(isWarm(Celsius { v: 30.0 }).$str())
    io::println(isWarm(Kelvin { v: 320.0 }).$str())
    0
}""", mode="run", title="`As<T>` as a bound"),
        S("""struct Celsius { v: f64 }
struct Fahrenheit { v: f64 }

bind Celsius into Fahrenheit {
    fn convert(&self) -> Fahrenheit { Fahrenheit { v: self.v * 1.8 + 32.0 } }
}

fn main() -> i64 {
    let c = Celsius { v: 100.0 }
    let f = c as Fahrenheit
    0
}""", mode="diag", title="`as` will not reach it"),
        S("""struct A { v: i64 }
struct B { v: i64 }

fn main() -> i64 {
    let a = A { v: 1 }
    let b = a into B
    b.v
}""", mode="diag", title="No conversion defined"),
        H("Where a conversion you wrote happens on its own"),
        P("A conversion is put in for you wherever the destination type is "
          "**written down**: an argument, an annotated binding, a field of a "
          "struct literal, a declared result, a `return`, an assignment into "
          "a typed place. Nowhere else. Nothing converts between two types "
          "that never said they convert, and `bind T into U` is the saying — "
          "so a conversion can always be found by searching for the binding "
          "that allows it."),
        S("""import std::io

struct Celsius { v: f64 }
struct Fahrenheit { v: f64 }

bind Celsius into Fahrenheit {
    fn convert(&self) -> Fahrenheit { Fahrenheit { v: self.v * 1.8 + 32.0 } }
}

struct Reading { at: Fahrenheit }

fn warmer(t: Fahrenheit) -> Fahrenheit { Fahrenheit { v: t.v + 1.0 } }
fn boiling() -> Fahrenheit { return Celsius { v: 100.0 } }

fn main() -> i64 {
    let annotated: Fahrenheit = Celsius { v: 100.0 }
    let argument = warmer(Celsius { v: 0.0 })
    let field = Reading { at: Celsius { v: 20.0 } }
    var place: Fahrenheit = Fahrenheit { v: 0.0 }
    place = Celsius { v: 10.0 }

    io::println(annotated.v.$str())
    io::println(argument.v.$str())
    io::println(field.at.v.$str())
    io::println(place.v.$str())
    io::println(boiling().v.$str())
    0
}""", mode="run", title="Five places the destination is written down"),
        P("The source does not have to be a type with a name. A tuple is a "
          "perfectly good thing to convert out of, which is how `(x, y)` "
          "comes to mean a point at every call site that takes one."),
        S("""import std::io

struct CGPoint { x: f64, y: f64 }
struct CGSize { width: f64, height: f64 }
struct CGRect { origin: CGPoint, size: CGSize }

bind (f64, f64) into CGPoint {
    fn convert(&self) -> CGPoint { CGPoint { x: self.0, y: self.1 } }
}
bind (f64, f64) into CGSize {
    fn convert(&self) -> CGSize { CGSize { width: self.0, height: self.1 } }
}

fn area(s: CGSize) -> f64 { s.width * s.height }

fn main() -> i64 {
    let r = CGRect { origin: (100.0, 100.0), size: (500.0, 300.0) }
    io::println(r.size.width.$str())
    io::println(area((2.0, 3.0)).$str())
    0
}""", mode="run", title="Converting out of a tuple"),
        N("A mark is always named, so a `bind` whose first type is not a name "
          "can only be the `into` form — there is nothing else it could "
          "mean. `bind` on a named type still reads as a mark, as it always "
          "did.", label="Why only `into` takes an unnamed source"),

        N("`As` is in scope everywhere — the prelude puts it there alongside "
          "`Option` and `Result`, so a binding never needs an import. It is "
          "declared in `std::convert`.", label="No import needed"),

        H("Conversions you have to ask for"),
        S("""import std::io

fn main() -> i64 {
    let big: i64 = 300
    let narrowed = big as u8            // truncates, so it must be explicit
    let rounded = 2.9 as i64            // toward zero
    let widened = 7 as f64
    let code = 'R' as i64               // a Character's scalar value
    let letter = 82 as Character
    let flag = 1 as bool
    let number = true as i64
    let text = "borrowed" as String     // CString to String

    io::println(narrowed)
    io::println(rounded)
    io::println(widened)
    io::println(code)
    io::println(letter)
    io::println(flag)
    io::println(number)
    io::println(text)
    0
}""", mode="run", title="Explicit casts with `as`"),
        N("Pointer casts, and any cast between a pointer and an integer, are "
          "unsafe operations. See **Safety**.", label="Unsafe casts", tone="warn"),
        S("""fn main() -> i64 {
    let text = "not a number"
    let wrong = text as i64
    0
}""", mode="diag", title="A cast that has no meaning"),

        H("There is no truthiness"),
        P("A condition must be a `bool`. Comparing explicitly is the only way, "
          "and the compiler says so when you forget."),
        S("""fn main() -> i64 {
    let count = 3
    if count {
        return 1
    }
    0
}""", mode="diag", title="An integer is not a condition"),
    ]))

# ===========================================================================
# 5. Operators
# ===========================================================================
SECTIONS.append(Sec(
    "operators", "expressions", "Operators",
    "The full set, in precedence order, with the mark method each one dispatches "
    "to when its operands are not builtin.",
    keywords=["precedence", "arithmetic", "bitwise", "shift", "comparison",
              "logical", "coalesce", "assignment", "unary", "range", "as", "is"],
    items=[
        H("Precedence"),
        P("Tighter binding first. Everything on one row associates left to "
          "right, except `??`, which associates right, and assignment, which "
          "associates right."),
        T(["Level", "Operators", "Kind"],
          [["tightest", "`f(x)` &nbsp; `a[i]` &nbsp; `a.b` &nbsp; `a?` &nbsp; `a.await`", "postfix"],
           ["", "`-a` &nbsp; `!a` &nbsp; `~a` &nbsp; `&a` &nbsp; `&var a` &nbsp; `*a`", "prefix"],
           ["", "`a as T` &nbsp; `a is T`", "cast and type test"],
           ["10", "`*` &nbsp; `/` &nbsp; `%`", "multiplicative"],
           ["9", "`+` &nbsp; `-`", "additive"],
           ["8", "`<<` &nbsp; `>>`", "shift"],
           ["7", "`&`", "bitwise and"],
           ["6", "`^`", "bitwise xor"],
           ["5", "`\\|`", "bitwise or"],
           ["4", "`==` &nbsp; `!=` &nbsp; `<` &nbsp; `<=` &nbsp; `>` &nbsp; `>=`", "comparison"],
           ["3", "`&&`", "logical and, short-circuiting"],
           ["2", "`\\|\\|`", "logical or, short-circuiting"],
           ["1", "`??`", "nil coalescing, right associative"],
           ["0", "`..` &nbsp; `..=`", "range"],
           ["loosest", "`=` and every compound form", "assignment, right associative"]],
          caption="Operator precedence, tightest first"),
        N("Bitwise operators bind **tighter** than comparison, so "
          "`flags & MASK == 0` parses as `flags & (MASK == 0)` would in C — "
          "except Rune rejects that outright, because `MASK == 0` is a `bool`. "
          "Write the parentheses.", label="Unlike C", tone="warn"),

        H("Arithmetic"),
        S("""import std::io

fn main() -> i64 {
    io::println(7 + 3)
    io::println(7 - 3)
    io::println(7 * 3)
    io::println(7 / 3)        // integer division truncates
    io::println(7 % 3)
    io::println(-7 / 3)
    io::println(-7 % 3)       // remainder keeps the dividend's sign
    io::println(7.0 / 2.0)
    io::println(-2.5)
    0
}""", mode="run", title="Integer and float arithmetic"),
        N("With `--safety=full` (the default) integer division and remainder "
          "check for a zero divisor and panic rather than trapping.",
          label="Division by zero"),

        H("Bitwise and shifts"),
        S("""import std::io

fn main() -> i64 {
    let flags: u8 = 0b1100
    io::println(flags & 0b1010)
    io::println(flags | 0b0011)
    io::println(flags ^ 0b1111)
    io::println(~flags)
    io::println(1 << 10)
    io::println(-16 >> 2)         // signed: arithmetic shift
    io::println(240u8 >> 2)       // unsigned: logical shift
    io::println(true & false)     // `&` `|` `^` also work on bool
    io::println(true ^ true)
    0
}""", mode="run", title="Bit manipulation"),

        H("Comparison"),
        P("Comparison yields a `bool`. Numbers, `bool`, `Character`, `String`, "
          "`CString`, pointers and payload-free enums all compare directly."),
        S("""import std::io

enum Colour { Red, Green, Blue }

fn main() -> i64 {
    io::println(1 < 2)
    io::println(2.5 >= 2.5)
    io::println('a' < 'b')
    io::println("abc" == "abc")
    io::println("abc" < "abd")       // lexicographic, byte-wise
    io::println(Colour::Red == Colour::Red)
    io::println(Colour::Red != Colour::Blue)
    io::println(true > false)
    0
}""", mode="run", title="Comparing every comparable thing"),

        H("Logical operators"),
        P("`&&` and `||` short-circuit: the right side is only evaluated when "
          "it can change the answer."),
        S("""import std::io

global var probes: i64 = 0

fn probe(value: bool) -> bool {
    probes += 1
    value
}

fn main() -> i64 {
    io::println(false && probe(true))    // right side skipped
    io::println(true || probe(true))     // right side skipped
    io::println("probes so far: " + probes.$str())

    io::println(true && probe(false))    // right side evaluated
    io::println(false || probe(true))    // right side evaluated
    io::println("probes now:   " + probes.$str())
    io::println(!true)
    0
}""", mode="run", title="Short-circuiting, demonstrated"),

        H("Nil coalescing"),
        P("`a ?? b` produces the value inside `a` when it has one, and `b` "
          "otherwise. It is right associative, so a chain falls through."),
        S("""import std::io

fn lookup(key: String) -> i64? {
    if key == "found" { return 7 }
    nil
}

fn main() -> i64 {
    io::println(lookup("found") ?? -1)
    io::println(lookup("missing") ?? -1)
    io::println(lookup("missing") ?? lookup("found") ?? -1)
    0
}""", mode="run", title="Defaulting an Option"),

        H("Assignment"),
        P("Every binary operator that makes sense has a compound form. A "
          "compound assignment follows exactly the rules of the operator it "
          "names, including operator overloads."),
        S("""import std::io

fn main() -> i64 {
    var n = 10
    n += 5;  io::println(n)
    n -= 3;  io::println(n)
    n *= 2;  io::println(n)
    n /= 4;  io::println(n)
    n %= 4;  io::println(n)
    n <<= 4; io::println(n)
    n >>= 2; io::println(n)
    n &= 12; io::println(n)
    n |= 3;  io::println(n)
    n ^= 5;  io::println(n)

    var text = "grow"
    text += "ing"
    io::println(text)
    0
}""", mode="run", title="Every compound assignment"),

        H("Ranges"),
        P("`a..b` excludes the upper bound, `a..=b` includes it. Ranges drive "
          "`for` loops, slicing, and range patterns."),
        S("""import std::io

fn count(lo: i64, hi: i64) -> i64 {
    var n = 0
    for i in lo..hi { n += 1 }
    n
}

fn main() -> i64 {
    io::println(count(0, 5))
    var inclusive = 0
    for i in 0..=5 { inclusive += 1 }
    io::println(inclusive)

    let values: [6:i64] = [1, 2, 3, 4, 5, 6]
    io::println(values[1..4].$length())
    io::println(values[1..=4].$length())
    io::println(values[3..].$length())        // open upper bound: to the end
    io::println(values[..2].$length())        // open lower bound: from the start
    0
}""", mode="run", title="Exclusive, inclusive and open ranges"),

        H("`as` and `is`"),
        P("`as` converts. `is` asks a class-typed or `dyn`-typed value what it "
          "actually is at run time."),
        S("""import std::io

class Shape { fn init(self) {} }
class Circle : Shape { fn init(self) { super.init() } }
class Square : Shape { fn init(self) { super.init() } }

fn describe(s: Shape) -> String {
    if s is Circle { return "circle" }
    if s is Square { return "square" }
    "some shape"
}

fn main() -> i64 {
    io::println(describe(Circle()))
    io::println(describe(Square()))
    io::println(describe(Shape()))
    0
}""", mode="run", title="Testing a dynamic type"),
    ]))

# ===========================================================================
# 6. Conditionals
# ===========================================================================
SECTIONS.append(Sec(
    "conditionals", "control flow", "Conditionals",
    "`if` is an expression, so it can produce a value. Every branch has to "
    "agree on the type of that value — when anything is going to use it.",
    keywords=["if", "elif", "else", "ternary", "is binding"],
    items=[
        H("`if` as a statement"),
        S("""import std::io

fn classify(n: i64) -> String {
    if n < 0 {
        "negative"
    } elif n == 0 {
        "zero"
    } elif n < 10 {
        "small"
    } else {
        "large"
    }
}

fn main() -> i64 {
    io::println(classify(-3))
    io::println(classify(0))
    io::println(classify(4))
    io::println(classify(400))
    0
}""", mode="run", title="`if` / `elif` / `else`"),
        N("`elif` is one word. `else if` also parses, and means the same thing.",
          label="Spelling"),

        H("`if` as an expression"),
        S("""import std::io

fn grade(score: i64) -> String {
    if score >= 90 { "A" }
    elif score >= 80 { "B" }
    elif score >= 70 { "C" }
    else { "F" }
}

fn main() -> i64 {
    let scores: [4:i64] = [95, 83, 71, 40]
    var line = ""
    for s in scores { line += grade(s) + " " }
    io::println(line)
    0
}""", mode="run", title="Choosing a value"),
        P("Used for its value, an `if` must have an `else`, and both branches "
          "must produce the same type."),
        S("""import std::io

fn main() -> i64 {
    let n = 7
    let label = if n % 2 == 0 { "even" } else { "odd" }
    let bigger = if n > 10 { n } else { 10 }

    // The branches can be whole blocks.
    let scaled = if n > 5 {
        let doubled = n * 2
        doubled + 1
    } else {
        0
    }

    io::println(label)
    io::println(bigger)
    io::println(scaled)
    0
}""", mode="run", title="Producing a value"),
        S("""fn main() -> i64 {
    let n = 7
    let mixed = if n > 0 { 1 } else { "one" }
    0
}""", mode="diag", title="Branches must agree"),

        H("Testing and binding at once"),
        P("`if value is Pattern` matches and, when the pattern binds names, "
          "makes them available in the body. It is the two-case form of "
          "`match`."),
        S("""import std::io

enum Reading { Missing, Celsius(f64), Fahrenheit(f64) }

fn asCelsius(r: Reading) -> f64 {
    if r is Reading::Celsius(c) {
        return c
    }
    if r is Reading::Fahrenheit(f) {
        return (f - 32.0) / 1.8
    }
    0.0
}

fn main() -> i64 {
    io::println(asCelsius(Reading::Celsius(21.5)))
    io::println(asCelsius(Reading::Fahrenheit(212.0)))
    io::println(asCelsius(Reading::Missing))

    // It works on Option too, which is just an enum.
    let maybe: i64? = 42
    if maybe is Some(v) {
        io::println("got " + v.$str())
    } else {
        io::println("nothing")
    }
    0
}""", mode="run", title="`is` with a binding pattern"),

        H("When the arms need not agree"),
        P("Every branch of an `if` or a `match` has to produce the same type "
          "\u2014 but only when something is going to *use* that type. In "
          "statement position nobody reads the result, so there is nothing "
          "for the arms to agree on, and each is checked on its own terms."),
        S('import std::io\n\nenum E { A, B, C }\n\nfn classify(n: i64) -> String {\n    // Used as this function\'s result, so the arms must still agree.\n    if n > 0 { "positive" } else { "negative" }\n}\n\nfn main() -> i64 {\n    let n = 5\n\n    // A statement: these arms produce `()`, `i64` and `String`, and that is\n    // fine, because nobody reads the result.\n    if n > 3 { io::println("big") }\n    elif n > 1 { 1 }\n    else { "small" }\n\n    match E::A {\n        E::A => io::println("a"),\n        E::B => 42,\n        E::C => "c",\n    }\n\n    io::println(classify(1))\n    0\n}', mode="run", title="Discarded branches, mixed arms"),
        T(["Where the branching expression sits", "Arms must agree"],
          [["a statement on its own", "no"],
           ["the tail of a loop body", "no \u2014 `while` and `for` produce "
            "`()`, and a `loop` produces what `break` carries"],
           ["a binding's initialiser", "**yes**"],
           ["a function's result", "**yes**"],
           ["an operand of something else", "**yes**"]]),
        N("The rule follows the value, not the syntax: an `if` nested inside a "
          "discarded one is still checked when *its* value is used \u2014 bound "
          "to a name, say. Only positions whose value is genuinely thrown away "
          "are relaxed.", label="It is about the value, not the shape"),

        H("An `if` with no `else` produces nothing"),
        P("The other half of the same rule. When the condition is false an "
          "`else`-less `if` has nothing to produce, so it is `()` \u2014 fine "
          "as a statement, and never usable as a value. The error names the "
          "`if`, not whatever was waiting on it:"),
        S("""fn wants(b: bool) -> bool { b }

fn main() -> i64 {
    let v: i64? = 42
    // The block is an argument, so its value is required.
    wants({
        if v is Some(n) {
            if n > 0 { n == 42 } else { false }
        }
    })
    0
}""", mode="diag", title="No `else`, but a value was wanted"),
        P("Three ways out, depending on what the code is really saying:"),
        T(["Instead of", "Write"],
          [["nested `if`s over an Option", "`match value { Some(v) => …, None "
            "=> … }`"],
           ["independent tests", "`a.hasValue() && a.unwrap() == x`"],
           ["a genuine two-way choice", "an explicit `else` on every branch"]]),
    ]))

# ===========================================================================
# 7. Loops
# ===========================================================================
SECTIONS.append(Sec(
    "loops", "control flow", "Loops",
    "Three loop forms. `loop` can carry a value out through `break`; `while` "
    "and `for` always evaluate to `()`.",
    keywords=["while", "loop", "for", "break", "continue", "label", "iterate",
              "range loop", "defer"],
    items=[
        H("`while`"),
        S("""import std::io

fn main() -> i64 {
    var n = 5
    var product = 1
    while n > 1 {
        product *= n
        n -= 1
    }
    io::println(product)
    0
}""", mode="run", title="A counted loop"),

        P("A `while` may destructure as well as test. `while value is Some(v)` "
          "runs for as long as the pattern matches, binding afresh each turn "
          "and releasing the binding at the end of every iteration."),
        S("""import std::io

struct Queue { items: [4:i64], taken: i64 }

extend Queue {
    pub fn next(&var self) -> i64? {
        if (*self).taken >= 4 { return nil }
        let v = (*self).items[(*self).taken]
        (*self).taken += 1
        v
    }
}

fn main() -> i64 {
    var q = Queue { items: [3, 1, 4, 1], taken: 0 }
    var total = 0
    while q.next() is Some(v) {
        io::println("got " + v.$str())
        total += v
    }
    io::println("total " + total.$str())
    0
}""", mode="run", title="Looping while a pattern matches"),

        H("`loop` and `break` with a value"),
        P("`loop` runs until something breaks out of it. Because `break` can "
          "carry a value, a `loop` is the natural way to write a search that "
          "produces a result."),
        S("""import std::io

fn firstPowerOfTwoAbove(limit: i64) -> i64 {
    var candidate = 1
    loop {
        if candidate > limit {
            break candidate
        }
        candidate *= 2
    }
}

fn main() -> i64 {
    io::println(firstPowerOfTwoAbove(100))
    io::println(firstPowerOfTwoAbove(1000))
    0
}""", mode="run", title="`loop` as an expression"),
        N("Only `loop` can produce a value this way. A `break` with a value "
          "inside a `while` or `for` is rejected, because those loops can also "
          "finish without breaking.", label="Why only `loop`"),

        H("`for`"),
        S("""import std::io

fn main() -> i64 {
    var line = ""
    for i in 0..4 { line += i.$str() }
    io::println(line)

    let names: [3:String] = ["ada", "grace", "alan"]
    for n in names { io::print(n + " ") }
    io::newline()

    var i = 0
    while i < names.$length() {
        io::println(i.$str() + ": " + names[i])
        i += 1
    }
    0
}""", mode="run", title="Three ways round a sequence"),
        P("`for` walks a range, an array or a slice natively, and anything "
          "bound to `Iterator` or `Sequence` besides — see "
          "[Iterators](#iterators). The loop variable is a pattern, so it can "
          "destructure as it goes."),
        S("""import std::io

fn main() -> i64 {
    var sum = 0
    for i in 1..=4 { sum += i }
    io::println(sum)

    let values: [4:i64] = [10, 20, 30, 40]
    var total = 0
    for v in values { total += v }
    io::println(total)

    // Over a slice, including one taken from an array.
    var half = 0
    for v in values[0..2] { half += v }
    io::println(half)

    // The binding is a pattern.
    let points: [3:(i64, i64)] = [(1, 2), (3, 4), (5, 6)]
    var crossSum = 0
    for (x, y) in points { crossSum += x * y }
    io::println(crossSum)
    0
}""", mode="run", title="Ranges, arrays, slices and patterns"),

        H("`break` and `continue`"),
        S("""import std::io

fn main() -> i64 {
    var evens = 0
    for i in 0..10 {
        if i % 2 == 1 { continue }
        if i > 6 { break }
        evens += 1
    }
    io::println(evens)
    0
}""", mode="run", title="Skipping and stopping"),

        H("Labelled loops"),
        P("A label is written before the loop as `name:` and referenced as "
          "`:name`. It lets `break` and `continue` reach past the innermost "
          "loop."),
        S("""import std::io

fn findPair(target: i64) -> (i64, i64) {
    var foundA = -1
    var foundB = -1
    outer: for a in 1..10 {
        for b in 1..10 {
            if a * b == target {
                foundA = a
                foundB = b
                break :outer
            }
        }
    }
    (foundA, foundB)
}

fn main() -> i64 {
    let (a, b) = findPair(42)
    io::println(a.$str() + " x " + b.$str())

    // `continue :label` restarts the outer loop.
    var trace = ""
    rows: for row in 0..3 {
        for col in 0..3 {
            if col == 1 { continue :rows }
            trace += row.$str() + col.$str() + " "
        }
    }
    io::println(trace)
    0
}""", mode="run", title="Escaping a nested loop"),

        H("`defer`"),
        P("`defer` schedules an expression to run when the enclosing block "
          "ends, whichever way it ends. Deferred expressions run in reverse "
          "order."),
        S("""import std::io

fn work(fail: bool) -> i64 {
    defer io::println("  cleanup A")
    defer io::println("  cleanup B")
    if fail {
        io::println("  bailing out")
        return -1
    }
    io::println("  finished")
    0
}

fn main() -> i64 {
    io::println("normal:")
    work(false)
    io::println("early return:")
    work(true)
    0
}""", mode="run", title="Cleanup that always runs"),
    ]))

# ===========================================================================
# 7b. Iterators
# ===========================================================================
SECTIONS.append(Sec(
    "iterators", "control flow", "Iterators",
    "`for` walks three shapes natively. Everything else it asks, through two "
    "marks that are in scope everywhere — because `for` is syntax, and what "
    "syntax dispatches through cannot need an import.",
    keywords=["Iterator", "Sequence", "for", "next", "iterate", "custom "
              "iterator", "associated type", "Item", "destructure", "chars",
              "generator", "cursor", "some Iterator"],
    items=[
        H("The two marks"),
        S("""mark Iterator {
    type Item
    fn next(&var self) -> Self::Item?          // the one thing to supply

    // Adaptors: each wraps this iterator in another one.
    fn map<B>(self, f: @function(Self::Item) -> B) -> Map<Self, B>
    fn filter(self, keep: @function(Self::Item) -> bool) -> Filter<Self>
    fn zip<J: Iterator>(self, other: J) -> Zip<Self, J>
    fn take_while(self, keep: @function(Self::Item) -> bool) -> TakeWhile<Self>
    fn skip_while(self, drop: @function(Self::Item) -> bool) -> SkipWhile<Self>
    fn take(self, count: i64) -> Take<Self>
    fn skip(self, count: i64) -> Skip<Self>
    fn enumerate(self) -> Enumerate<Self>
    fn chain<J: Iterator>(self, other: J) -> Chain<Self, J>
    fn as_iter(self) -> Self                   // already one; here for symmetry

    // Ending a chain: these are what run it.
    fn collect(self) -> vector::Vector<Self::Item>
    fn count(self) -> i64
    fn find(self, keep: @function(Self::Item) -> bool) -> Self::Item?
    fn any(self, keep: @function(Self::Item) -> bool) -> bool
    fn all(self, keep: @function(Self::Item) -> bool) -> bool
}

mark Sequence {
    type Iter: Iterator
    fn iterate(&self) -> Self::Iter

    // Every adaptor again, one step earlier: each asks for a cursor first.
    // `as_iter` is `iterate` under the name a chain reads better with.
}""", mode="frag", title="What `std::iter` declares"),
        P("An `Iterator` is a cursor: `next` hands back a value each turn and "
          "`nil` when it runs out. A `Sequence` is something a fresh cursor "
          "can be had from. `Iterator` is asked first, so a type that is both "
          "stays its own iterator."),

        H("Writing one"),
        S("""import std::io

struct Countdown { pub remaining: i64 }

bind Iterator to Countdown {
    type Item = i64
    fn next(&var self) -> Self::Item? {
        if self.remaining <= 0 { return nil }     // `nil` ends the loop
        self.remaining -= 1
        self.remaining + 1
    }
}

/// A series is often clearest as an iterator: the state is named, and the
/// loop that consumes it does not have to know any of it.
struct Fibonacci { pub limit: i64, pub a: i64, pub b: i64 }

fn fibonacci(limit: i64) -> Fibonacci { Fibonacci { limit: limit, a: 0, b: 1 } }

bind Iterator to Fibonacci {
    type Item = i64
    fn next(&var self) -> Self::Item? {
        if self.a >= self.limit { return nil }
        let value = self.a
        let onward = self.a + self.b
        self.a = self.b
        self.b = onward
        value
    }
}

fn main() -> i64 {
    for n in (Countdown { remaining: 5 }) { io::print(n.$str() + " ") }
    io::newline()
    for n in fibonacci(100) { io::print(n.$str() + " ") }
    io::newline()
    0
}""", mode="run", title="Two hand-written iterators"),
        N("A `for` sequence cannot be a bare struct literal — the `{` would "
          "start the loop body. Name it first, or wrap it in parentheses."),

        H("Writing a sequence"),
        P("A container binds `Sequence` rather than `Iterator`, because a "
          "container is not a cursor over itself. It hands out a fresh one, so "
          "two loops over the same container — nested loops included — do not "
          "interfere."),
        S("""import std::io

struct Grid { pub width: i64, pub height: i64 }
struct GridCells { pub grid: Grid, pub at: i64 }

bind Sequence to Grid {
    type Iter = GridCells
    fn iterate(&self) -> GridCells { GridCells { grid: *self, at: 0 } }
}

bind Iterator to GridCells {
    type Item = (i64, i64)
    fn next(&var self) -> Self::Item? {
        if self.at >= self.grid.width * self.grid.height { return nil }
        let cell = (self.at % self.grid.width, self.at / self.grid.width)
        self.at += 1
        cell
    }
}

fn main() -> i64 {
    let board = Grid { width: 3, height: 2 }
    // The binding is a pattern, so a cell arrives already taken apart.
    for (x, y) in board { io::print("(" + x.$str() + "," + y.$str() + ") ") }
    io::newline()
    for (x, y) in board { io::print((x + y).$str() + " ") }
    io::newline()
    0
}""", mode="run", title="A container hands out a cursor"),
        N("The bind that satisfies `type Iter: Iterator` may be written after "
          "the one that needs it, as it is here. Declaration order does not "
          "decide whether the program compiles."),

        H("What the loop advances"),
        P("`for` puts the iterator in a slot of its own and advances that, "
          "which is what a binding would have got: a struct iterator is "
          "copied, so the one you named is untouched afterwards; a class "
          "iterator is shared, so it is left spent. Neither is special to "
          "`for` — both follow from what the type already means."),
        S("""import std::io

struct Ticks { pub left: i64 }
class Cursor {
    pub left: i64
    fn init(self, left: i64) { self.left = left }
}

bind Iterator to Ticks {
    type Item = i64
    fn next(&var self) -> Self::Item? {
        if self.left <= 0 { return nil }
        self.left -= 1
        self.left
    }
}
bind Iterator to Cursor {
    type Item = i64
    fn next(&var self) -> Self::Item? {
        if self.left <= 0 { return nil }
        self.left -= 1
        self.left
    }
}

fn main() -> i64 {
    var value = Ticks { left: 3 }
    for _ in value { }
    io::println("struct, after the loop: " + value.left.$str())

    let shared = Cursor(3)
    for _ in shared { }
    io::println("class,  after the loop: " + shared.left.$str())
    0
}""", mode="run", title="Copied, or shared"),
        P("The loop also keeps whatever it walks alive for as long as the walk "
          "lasts, so iterating a temporary is safe."),

        H("Destructuring"),
        P("The loop binding is a pattern. It has to match every value, because "
          "every turn has to bind — a pattern that could fail is refused, and "
          "the value should be taken apart with `match` inside the body "
          "instead."),
        S("""import std::io

struct Entry { pub key: String, pub value: i64 }

fn main() -> i64 {
    let pairs: [2:(i64, String)] = [(1, "one"), (2, "two")]
    for (n, name) in pairs { io::println(n.$str() + " = " + name) }

    let entries: [2:Entry] = [
        Entry { key: "a", value: 1 },
        Entry { key: "b", value: 2 },
    ]
    for Entry { key, value } in entries { io::println(key + " -> " + value.$str()) }

    let nested: [2:((i64, i64), String)] = [((1, 2), "first"), ((3, 4), "second")]
    for ((a, b), label) in nested {
        io::println(label + ": " + (a * b).$str())
    }

    var turns = 0
    for _ in pairs { turns += 1 }
    io::println(turns)
    0
}""", mode="run", title="Patterns in the loop head"),
        S("""import std::io

enum Colour { Red, Green }

fn main() -> i64 {
    let cs: [2:Colour] = [Colour::Red, Colour::Green]
    for Colour::Red in cs { io::println("red") }
    0
}""", mode="diag", title="A pattern that could fail"),


        H("Reshaping one"),
        P("Every `Iterator` carries the adaptors, and so does every "
          "`Sequence` — which asks for a cursor first. Each wraps an iterator "
          "in another iterator."),
        T(["Written", "What it yields"],
          [["`.map(f)`", "every value with `f` applied to it"],
           ["`.filter(keep)`", "only the values `keep` says yes to"],
           ["`.zip(other)`", "pairs, ending as soon as either side does"],
           ["`.take_while(keep)`",
            "the values up to the first `false`, and no further"],
           ["`.skip_while(drop)`",
            "everything from the first value `drop` says no to onwards"],
           ["`.take(n)`", "at most `n` values"],
           ["`.skip(n)`", "everything after the first `n`"],
           ["`.enumerate()`", "each value paired with its position"],
           ["`.chain(other)`", "this one's values, then the other's"],
           ["`.as_iter()`",
            "a cursor — `iterate` on a `Sequence`, and itself on an "
            "`Iterator`, so a chain reads the same either way"]],
          caption="The adaptors"),
        S("""import std::io
import std::collections::vector

struct Person { pub name: String, pub age: i64 }

fn main() -> i64 {
    var people = vector::Vector<Person>()
    people.push(Person { name: "ada", age: 36 })
    people.push(Person { name: "tom", age: 11 })
    people.push(Person { name: "grace", age: 45 })

    for name in people.filter(||(p: &Person) -> bool { p.age >= 18 })
                      .map(||(p: &Person) -> String { p.name.$clone() }) {
        io::println(name)
    }
    0
}""", mode="run", title="A chain over a container"),
        N("Nothing is computed on the way in. A value moves through the chain "
          "only when the loop at the end asks for it, so a chain over a "
          "million elements allocates nothing and walks the source once."),

        H("Ending a chain"),
        P("The adaptors compute nothing. Something has to ask, and these are "
          "what ask: `collect` runs the whole chain into a `Vector`, and the "
          "rest answer a question about it. `find`, `any` and `all` stop as "
          "soon as the answer is settled, so a chain over something endless "
          "still ends."),
        T(["Written", "Hands back"],
          [["`.collect()`", "everything left, in a `vector::Vector`"],
           ["`.count()`", "how many values are left; walks to the end"],
           ["`.find(keep)`", "the first value `keep` says yes to, or `nil`"],
           ["`.any(keep)`", "true at the first yes"],
           ["`.all(keep)`", "false at the first no"]],
          caption="Running a chain out"),
        S("""import std::io
import std::iter
import std::collections::vector

fn main() -> i64 {
    var numbers = vector::Vector<i64>()
    var n = 1
    while n <= 6 { numbers.push(n); n += 1 }

    let evens = numbers.filter(||(v: i64) -> bool { v % 2 == 0 }).collect()
    io::println(evens)

    io::println(numbers.as_iter().skip(2).take(3).collect())
    io::println(numbers.as_iter().count())
    io::println(numbers.as_iter().any(||(v: i64) -> bool { v > 5 }))
    io::println(numbers.as_iter().all(||(v: i64) -> bool { v > 5 }))
    match numbers.as_iter().find(||(v: i64) -> bool { v % 4 == 0 }) {
        Some(v) => io::println("first multiple of four: " + v.$str()),
        None    => io::println("none"),
    }

    for (i, v) in numbers.enumerate() { io::print(i.$str() + ":" + v.$str() + " ") }
    io::newline()
    0
}""", mode="run", title="Running a chain out"),
        N("`vector::collect(it)` is the same thing written the other way "
          "round, and still there. The method is what a chain reads better "
          "with; the free function is what `std::iter` is written against, "
          "because a mark cannot depend on a container that binds it."),

        H("Stopping early"),
        P("`take_while` ends at the first value its test rejects and never "
          "asks the source again — the source is advanced exactly once past "
          "the last value taken. That is the difference from `filter`, which "
          "skips a value it does not want and carries on, and it is what lets "
          "a `take_while` sit in front of an iterator that never ends."),
        S("""import std::io
import std::iter
import std::collections::vector

fn main() -> i64 {
    var v = vector::Vector<i64>()
    v.push(1); v.push(2); v.push(3); v.push(10); v.push(4)

    // The 4 after the 10 is not taken: the run ended at the 10.
    io::println(v.take_while(||(n: i64) -> bool { n < 5 }).collect())
    // `filter` skips the 10 and keeps going.
    io::println(v.filter(||(n: i64) -> bool { n < 5 }).collect())
    // `skip_while` is the mirror: everything from the first `false` onwards.
    io::println(v.skip_while(||(n: i64) -> bool { n < 3 }).collect())

    // `counting` never ends. `take_while` is what ends it.
    io::println(iter::counting(1).take_while(||(n: i64) -> bool { n * n < 50 }).collect())
    0
}""", mode="run", title="`take_while` against `filter`"),

        H("Numbering"),
        P("`0..n` is loop syntax rather than a value, so `iter::counting` is "
          "what numbers something. It never ends, which is safe because `zip` "
          "stops with the shorter side — and `enumerate` is the same pairing "
          "without the second iterator."),
        S("""import std::io
import std::iter
import std::collections::vector

fn main() -> i64 {
    var names = vector::Vector<String>()
    names.push("ada")
    names.push("grace")
    names.push("edsger")

    for (i, name) in iter::counting(1).zip(names.iterate()) {
        io::println(i.$str() + ". " + name)
    }
    for (i, name) in names.enumerate() {
        io::println(i.$str() + " -> " + name)
    }
    0
}""", mode="run", title="`counting`, `zip` and `enumerate`"),
        S("""import std::io

struct Countdown { pub remaining: i64 }

bind Iterator to Countdown {
    type Item = i64
    fn next(&var self) -> Self::Item? {
        if self.remaining <= 0 { return nil }
        self.remaining -= 1
        self.remaining + 1
    }
}

fn main() -> i64 {
    // `zip` ends as soon as either side does, and does not advance the longer
    // one past the pair it produced.
    for (a, b) in (Countdown { remaining: 2 }).zip(Countdown { remaining: 9 }) {
        io::println(a.$str() + "/" + b.$str())
    }
    0
}""", mode="run", title="`zip` ends with the shorter side"),

        H("Returning a type parameterised by `Self`"),
        P("`map` returns `Map<Self, B>` and `filter` returns `Filter<Self>`. "
          "Every type carrying the mark gets those methods, and the wrappers "
          "carry the mark too — so resolving the results for every type would "
          "never finish. They are resolved where the method is *called*, "
          "which is why `Map<Filter<Ticks>, String>` exists exactly when a "
          "chain asks for it. Nothing has to be declared to get this; it is "
          "how such a signature is handled, and it works for a mark of your "
          "own just as well."),
        S("""import std::io

struct Boxed<I> { pub inner: I }

mark Wrapping {
    fn step(&var self) -> i64?

    // The result wraps `Self`, and `Boxed<I>` carries the mark as well.
    fn boxed(self) -> Boxed<Self> { Boxed<Self> { inner: self } }
}

bind<I: Wrapping> Wrapping to Boxed<I> {
    fn step(&var self) -> i64? { self.inner.step() }
}

struct Ticks { pub left: i64 }
bind Wrapping to Ticks {
    fn step(&var self) -> i64? {
        if self.left <= 0 { return nil }
        self.left -= 1
        self.left
    }
}

fn main() -> i64 {
    // Three deep, because this line asks for three — and no deeper.
    var b = (Ticks { left: 2 }).boxed().boxed().boxed()
    io::println(b.step().or(-1))
    io::println(b.step().or(-1))
    io::println(b.step().or(-1))
    0
}""", mode="run", title="A mark of your own, doing the same thing"),
        N("What has no end is a type parameterised by a *deeper* version of "
          "itself — `struct Nest<T> { deeper: Nest<Nest<T>>? }`. That is "
          "reported rather than run out of stack. A recursive structure holds "
          "itself, `Nest<T>?`, which is fine."),
        P("A function that *returns* a chain does not have to write the "
          "wrapper type out. `-> some Iterator` is the spelling: the body "
          "still produces one concrete type, callers see only the mark, and "
          "there is no box. See [Opaque results: `some Mark`](#marks)."),

        H("From the library"),
        T(["Written", "Walks"],
          [["`for v in vec`", "a `Vector<T>`, through `VectorIter<T>`"],
           ["`for (k, v) in map`", "a `Map<K, V>`, as pairs"],
           ["`for c in text::chars(s)`",
            "the characters of a `String`, in one linear pass"],
           ["`iter::counting(n)`", "integers from `n`, endlessly"],
           ["`iter::countingBy(n, step)`", "the same, in steps"],
           ["`it.collect()`", "runs an iterator into a `Vector`"],
           ["`vector::collect(it)`", "the same, written the other way round"]]),
        S("""import std::io
import std::text
import std::collections::vector

fn main() -> i64 {
    var names = vector::Vector<String>()
    names.push("ada")
    names.push("grace")
    for name in names { io::println(name) }

    for c in text::chars("héllo") { io::print(c.$str() + "·") }
    io::newline()
    0
}""", mode="run", title="The containers that come with it"),

        H("Iterating generically"),
        P("The marks are bounds like any other: one takes anything a `for` can "
          "walk, the other takes a cursor."),
        S("""import std::io
import std::collections::vector

fn count<S: Sequence>(s: S) -> i64 {
    var n = 0
    for _ in s { n += 1 }
    n
}

struct Ticks { pub left: i64 }
bind Iterator to Ticks {
    type Item = i64
    fn next(&var self) -> Self::Item? {
        if self.left <= 0 { return nil }
        self.left -= 1
        self.left
    }
}

fn drain<I: Iterator>(it: I) -> i64 {
    var n = 0
    for _ in it { n += 1 }
    n
}

fn main() -> i64 {
    var v = vector::Vector<i64>()
    v.push(1)
    v.push(2)
    io::println(count(v))
    io::println(drain(Ticks { left: 4 }))
    0
}""", mode="run", title="Bounds that name the marks"),
        N("A `dyn Iterator` cannot be iterated: a mark object does not carry "
          "the binding's associated types, so there is nothing for the loop "
          "variable to be. Iterate the concrete type."),
        P("`flatten` and `flatMap` lay an iterator of iterators out flat. "
          "Their item type is two marks deep — the item of the source's item, "
          "which `I::Item::Item` spells — and the binding that supplies it is "
          "conditional on `where I::Item: Iterator`, a clause whose subject is "
          "itself a projection."),
        S("""import std::io
import std::text
import std::collections::vector

fn main() -> i64 {
    let rows = vec!(vec!(1, 2), vec!(3), vec!())
    let flat = rows.flatMap(||(r: &vector::Vector<i64>) -> vector::VectorIter<i64> { r.as_iter() })
    io::println(flat.fold(0, ||(acc: i64, x: i64) -> i64 { acc + x }))

    let names = vec!("ab", "cd")
    var letters = ""
    for c in names.flatMap(||(n: &String) -> text::Chars { text::chars(n.$clone()) }) {
        letters += c.$str()
    }
    io::println(letters)
    0
}""", mode="run", title="Flattening a vector of vectors"),
        N("There is no `sum`, `min` or `max` yet: each waits on a way to name "
          "a type's zero and its ordering as a bound. `fold` and `best` cover "
          "both in the meantime.",
          label="What is not here"),
    ]))

# ===========================================================================
# 8. Pattern matching
# ===========================================================================
SECTIONS.append(Sec(
    "match", "control flow", "Pattern matching",
    "`match` is exhaustive: the compiler lists the cases you have not handled. "
    "Patterns work in `match`, in `if ... is`, in `for`, and on the left of a "
    "binding.",
    keywords=["match", "pattern", "guard", "wildcard", "or pattern", "range "
              "pattern", "destructure", "exhaustive", "binding pattern"],
    items=[
        H("Every kind of pattern"),
        S("""import std::io

enum Move { Rock, Paper, Scissors }

fn sameShape(a: Move, b: Move) -> bool { (a as i64) == (b as i64) }

fn beats(a: Move, b: Move) -> String {
    match (a, b) {
        (Move::Rock, Move::Scissors) => "rock wins",
        (Move::Paper, Move::Rock) => "paper wins",
        (Move::Scissors, Move::Paper) => "scissors win",
        (x, y) if sameShape(x, y) => "a draw",
        _ => "the other one wins",
    }
}

fn main() -> i64 {
    io::println(beats(Move::Rock, Move::Scissors))
    io::println(beats(Move::Rock, Move::Rock))
    io::println(beats(Move::Rock, Move::Paper))
    0
}""", mode="run", title="Matching a pair, with a guard"),
        S("""import std::io

struct Point { x: i64, y: i64 }
enum Shape {
    Empty,
    Circle(f64),
    Rect { width: i64, height: i64 },
}

fn describeNumber(n: i64) -> String {
    match n {
        0 => "zero",                 // literal
        1 | 2 | 3 => "small",        // alternatives
        4..=9 => "single digit",     // inclusive range
        10..100 => "double digit",   // exclusive range
        x if x < 0 => "negative",    // guard
        _ => "large",                // wildcard
    }
}

fn describeShape(s: Shape) -> String {
    match s {
        Shape::Empty => "empty",
        Shape::Circle(r) => "circle r=" + r.$str(),
        Shape::Rect { width, height } => "rect " + width.$str() + "x" + height.$str(),
    }
}

fn describePoint(p: Point) -> String {
    match p {
        Point { x, y } => "(" + x.$str() + ", " + y.$str() + ")",
    }
}

fn describeTuple(t: (i64, String)) -> String {
    match t {
        (0, name) => "zero named " + name,
        (n, name) => name + "=" + n.$str(),
    }
}

fn main() -> i64 {
    io::println(describeNumber(0) + " " + describeNumber(2) + " " +
                describeNumber(7) + " " + describeNumber(42) + " " +
                describeNumber(-1) + " " + describeNumber(1000))
    io::println(describeShape(Shape::Empty))
    io::println(describeShape(Shape::Circle(1.5)))
    io::println(describeShape(Shape::Rect { width: 3, height: 4 }))
    io::println(describePoint(Point { x: 1, y: 2 }))
    io::println(describeTuple((0, "origin")))
    io::println(describeTuple((5, "five")))
    0
}""", mode="run", title="Literals, ranges, alternatives, guards, destructuring"),
        T(["Pattern", "Matches"],
          [["`_`", "anything, binding nothing"],
           ["`name`", "anything, binding it to `name`"],
           ["`var name`", "anything, binding it mutably"],
           ["`42` `\"text\"` `'c'` `true`", "that exact value"],
           ["`1 \\| 2 \\| 3`", "any of the alternatives"],
           ["`1..10`", "the range, upper bound excluded"],
           ["`1..=10`", "the range, upper bound included"],
           ["`Enum::Unit`", "a variant with no payload"],
           ["`Enum::Tuple(a, b)`", "a tuple-shaped variant, binding its fields"],
           ["`Enum::Struct { a, b }`", "a struct-shaped variant, binding by name"],
           ["`Struct { a, b }`", "a struct, binding its fields"],
           ["`Struct { a, .. }`", "a struct, ignoring the rest"],
           ["`(a, b)`", "a tuple of that arity"],
           ["`[a, b, c]`", "an array or slice of exactly three, binding each"],
           ["`[first, ..rest]`", "one or more, binding the remainder as a slice"],
           ["`[first, .., last]`", "two or more, ignoring the middle"],
           ["`&inner`", "through a borrow"],
           ["`name @ 1..10`", "the range, and binds the whole value too"],
           ["`pattern if cond`", "the pattern, when the guard also holds"]],
          caption="Every pattern form"),

        H("Slice and array patterns"),
        P("`[a, b, c]` takes an array or a slice apart element by element. "
          "`..` stands for a run in the middle — any length, including none — "
          "and `..rest` binds that run as a slice into the same storage. The "
          "elements before `..` are matched from the front and the ones after "
          "it from the back, so `[first, .., last]` reaches both ends of "
          "anything with at least two."),
        S("""import std::io

fn describe(xs: [i64]) -> String {
    match xs {
        [] => "empty",
        [x] => "one: " + x.$str(),
        [a, b] => "two: " + a.$str() + " " + b.$str(),
        [first, .., last] => "many: " + first.$str() + ".." + last.$str(),
    }
}

fn sum(xs: [i64]) -> i64 {
    match xs {
        [] => 0,
        [head, ..tail] => head + sum(tail),
    }
}

fn command(words: [String]) -> String {
    match words {
        ["go", dir] => "going " + dir,
        ["say", ..rest] => "saying " + rest.$length().$str() + " words",
        [.., "end"] => "ends with end",
        _ => "unknown",
    }
}

fn main() -> i64 {
    let empty: [0:i64] = []
    io::println(describe(empty))
    io::println(describe([7, 8]))
    io::println(describe([1, 2, 3, 4]))
    io::println(sum([1, 2, 3, 4]))
    let words: [2:String] = ["go", "north"]
    io::println(command(words))
    let more: [3:String] = ["say", "a", "b"]
    io::println(command(more))
    0
}""", mode="run", title="Matching a slice by shape"),
        P("A slice is matched by length, and a `match` over one is exhaustive "
          "when every length has an arm: an arm without `..` covers one "
          "length exactly, and one with `..` covers every length from its "
          "minimum up. A fixed array's length is known, so a pattern that "
          "accounts for every element is irrefutable — it works in `let` and "
          "`for`, and one that cannot line up is an error rather than a test "
          "that fails."),
        S("""import std::io

fn main() -> i64 {
    let xs: [3:i64] = [1, 2, 3]
    let [a, b, c] = xs                 // the count matches, so it cannot fail
    io::println(a + b + c)
    let [head, ..tail] = xs
    io::println(head.$str() + " then " + tail.$length().$str() + " more")

    let pairs: [2:[2:i64]] = [[1, 2], [3, 4]]
    for [x, y] in pairs { io::println(x * 10 + y) }

    if xs is [1, ..rest] { io::println("starts with 1, then " + rest.$length().$str()) }
    0
}""", mode="run", title="Irrefutable against a fixed array"),
        S("""fn main() -> i64 {
    let xs: [3:i64] = [1, 2, 3]
    let [a, b] = xs
    a + b
}""", mode="diag", title="A count that cannot line up"),
        S("""fn first(xs: [i64]) -> i64 {
    match xs {
        [] => 0,
        [a] => a,
        [a, b, c, ..] => a,
    }
}

fn main() -> i64 { first([1]) }""", mode="diag", title="A length no arm accepts"),

        H("Bindings in alternatives"),
        P("Alternatives may bind, as long as every one of them binds the same "
          "names with the same types. That makes it possible to collapse "
          "variants that carry the same payload."),
        S("""import std::io

enum Token {
    Int(i64),
    Nat(i64),
    Word(String),
    Blank,
}

fn weight(t: Token) -> i64 {
    match t {
        Token::Int(n) | Token::Nat(n) => n,
        Token::Word(w) => w.$length(),
        Token::Blank => 0,
    }
}

enum Boxed { Small { size: i64 }, Large { size: i64 } }

fn size(b: Boxed) -> i64 {
    match b {
        Boxed::Small { size } | Boxed::Large { size } => size,
    }
}

fn main() -> i64 {
    io::println(weight(Token::Int(7)))
    io::println(weight(Token::Nat(9)))
    io::println(weight(Token::Word("abcd")))
    io::println(weight(Token::Blank))
    io::println(size(Boxed::Large { size: 12 }))
    0
}""", mode="run", title="`A(n) | B(n)` binds `n` either way"),
        S("""enum T { A(i64), B(String) }

fn f(t: T) -> i64 {
    match t {
        T::A(n) | T::B(n) => 0,
    }
}

fn main() -> i64 { 0 }""", mode="diag", title="The same name must have the same type"),

        H("Exhaustiveness"),
        P("A `match` over an enum has to cover every variant. The compiler "
          "names the ones you missed and points at the declaration."),
        S("""enum Colour { Red, Green, Blue }

fn name(c: Colour) -> String {
    match c {
        Colour::Red => "red",
        Colour::Green => "green",
    }
}

fn main() -> i64 { 0 }""", mode="diag", title="A missing variant"),

        H("Matching through a borrow"),
        P("`match` looks through borrows, so a `&self` method can match on "
          "`self` directly without dereferencing first."),
        S("""import std::io

enum Shape { Circle(f64), Square(f64) }

mark Area { fn area(&self) -> f64 }

bind Area to Shape {
    fn area(&self) -> f64 {
        match self {                       // `self` is `&Shape` here
            Shape::Circle(r) => 3.14159 * r * r,
            Shape::Square(s) => s * s,
        }
    }
}

fn label(s: &Shape) -> String {
    match s {                              // and here it is a parameter
        Shape::Circle(r) => "circle",
        Shape::Square(s) => "square",
    }
}

fn main() -> i64 {
    let c = Shape::Circle(2.0)
    io::println(label(&c) + " " + c.area().$str())
    0
}""", mode="run", title="No explicit dereference needed"),

        H("`match` produces a value"),
        P("Like `if`, a `match` used for its value needs every arm to agree on "
          "a type. An arm body may be a block."),
        S("""import std::io

enum Level { Debug, Info, Warning, Error }

fn severity(l: Level) -> i64 {
    match l {
        Level::Debug => 10,
        Level::Info => 20,
        Level::Warning => {
            let base = 30
            base + 0
        }
        Level::Error => 40,
    }
}

fn main() -> i64 {
    io::println(severity(Level::Debug))
    io::println(severity(Level::Warning))
    io::println(severity(Level::Error))
    0
}""", mode="run", title="An arm can be a block"),
    ]))

DOC = {
    "version": "v0.1.0",
    "tagline": "A low-level, statically typed language with automatic reference "
               "counting and optional memory safety. This is the complete "
               "reference — every construct, every variant, with examples "
               "compiled by `runec` as this page was built.",
    "footer": "Rune reference · generated from `docs/reference/content.py`",
    "sections": SECTIONS,
}

# ===========================================================================
# 9. Functions
# ===========================================================================
SECTIONS.insert(len(SECTIONS) - 0, Sec(
    "functions", "declarations", "Functions",
    "Parameters may have defaults and be passed by label. The last expression "
    "is the result. Functions nest, and a function is a value.",
    keywords=["fn", "parameter", "default", "label", "return", "nested",
              "recursion", "variadic", "implicit return", "Never", "diverging"],
    items=[
        H("Declaring a function"),
        S("""import std::io

fn add(a: i64, b: i64) -> i64 {
    a + b                        // the last expression is the result
}

fn shout(text: String) -> String {
    return text + "!"            // `return` works too, and exits early
}

fn log(message: String) {        // no `->` means the result is ()
    io::println("log: " + message)
}

fn main() -> i64 {
    io::println(add(20, 22))
    io::println(shout("hey"))
    log("done")
    0
}""", mode="run", title="Result, early return, and no result"),

        H("`Never`: a function that does not return"),
        P("`Never` is the type of an expression that does not come back. "
          "`process::exit`, `succeed`, `fail` and `panic` are declared "
          "`-> Never`, and a `Never` converts to any type — so a call to one "
          "can stand at the end of a function that promised a value, or in "
          "the branch of an `if` or `match` that has nothing to produce. No "
          "`return` is needed after it: there is no after."),
        S("""import std::io
import std::process

fn pick(n: i64) -> i64 {
    if n > 0 { return n }
    process::panic("negative")     // `Never` fills the `i64` this promised
}

fn describe(n: i64) -> String {
    if n == 1 { "one" } else { process::fail() }
}

fn main() -> i64 {
    io::println(pick(3))
    io::println(describe(1))
    let x: i64 = if true { 5 } else { process::panic("no") }
    io::println(x)
    0
}""", mode="run", title="A diverging call where a value was expected"),
        P("A function of your own may be declared `-> Never`. Every path has "
          "to end in something that does not return; falling off the end with "
          "a value is a type error."),
        S("""import std::io

fn bad() -> Never { io::println("x") }

fn main() -> i64 { bad() }""", mode="diag", title="Falling off the end of `-> Never`"),
        N("`exit` and `panic` leave immediately: no `deinit` runs. That is "
          "the difference from returning — a `Never` is not an empty value, "
          "it is the absence of one.",
          label="Nothing is cleaned up"),

        H("Default arguments"),
        P("A parameter may have a default. Defaults are evaluated at the call "
          "site, once per call that omits the argument."),
        S("""import std::io

fn indent(text: String, width: i64 = 2, fill: String = " ") -> String {
    fill.$repeat(width) + text
}

fn main() -> i64 {
    io::println(indent("a"))
    io::println(indent("b", 6))
    io::println(indent("c", 4, "."))
    0
}""", mode="run", title="Trailing defaults"),

        H("Labelled arguments"),
        S("""import std::io

// A label makes the call read like a sentence, and stops two arguments of the
// same type from being swapped by accident.
fn transfer(source: i64, target: i64, amount: i64) -> String {
    "moved " + amount.$str() + " from " + source.$str() +
    " to " + target.$str()
}

fn main() -> i64 {
    io::println(transfer(source: 1, target: 9, amount: 3))
    io::println(transfer(amount: 3, target: 9, source: 1))   // any order
    0
}""", mode="run", title="Labels may be given in any order"),
        P("Any argument may be passed by name. Labels and positions mix freely, "
          "and a label lets you skip past a default."),
        S("""import std::io

fn box(text: String, width: i64 = 10, pad: String = "-") -> String {
    pad.$repeat(width) + text + pad.$repeat(width)
}

fn main() -> i64 {
    io::println(box("all positional", 3, "="))
    io::println(box("all labelled", width: 3, pad: "="))
    io::println(box("mixed", pad: "*"))          // skips `width`
    io::println(box(text: "reordered", pad: "+", width: 2))
    0
}""", mode="run", title="Naming arguments at the call site"),
        S("""fn greet(name: String, greeting: String = "hello") -> String {
    greeting + ", " + name
}

fn main() -> i64 {
    greet(nmae: "typo")
    0
}""", mode="diag", title="A label that does not exist"),

        H("Recursion and nesting"),
        P("A function may be declared inside another. It behaves like a private "
          "module-level function: it sees no locals from its parent, which is "
          "what separates it from a closure."),
        S("""import std::io

fn factorial(n: i64) -> i64 {
    if n <= 1 { 1 } else { n * factorial(n - 1) }
}

fn fibonacci(n: i64) -> i64 {
    fn step(a: i64, b: i64, left: i64) -> i64 {
        if left == 0 { a } else { step(b, a + b, left - 1) }
    }
    step(0, 1, n)
}

fn main() -> i64 {
    io::println(factorial(10))
    io::println(fibonacci(20))
    0
}""", mode="run", title="Recursive and nested functions"),

        H("Mutable parameters"),
        P("A parameter is immutable unless declared `var`. A `var` parameter is "
          "a local copy, so changing it does not affect the caller — pass "
          "`&var T` for that."),
        S("""import std::io

fn countdown(var n: i64) -> String {
    var out = ""
    while n > 0 {
        out += n.$str() + " "
        n -= 1
    }
    out
}

fn bump(target: &var i64) {
    *target += 1
}

fn main() -> i64 {
    let start = 3
    io::println(countdown(start))
    io::println(start)              // untouched: `var n` was a copy

    var counter = 0
    bump(&var counter)
    bump(&var counter)
    io::println(counter)            // changed: passed by mutable borrow
    0
}""", mode="run", title="`var` parameter versus `&var` borrow"),
    ]))

# ===========================================================================
# 10. Closures
# ===========================================================================
SECTIONS.append(Sec(
    "closures", "declarations", "Closures and function values",
    "A closure is written `||(params) -> Result { ... }`. It captures by "
    "value, a named function converts to a function value on its own, and "
    "the types may be left out wherever the context already says them.",
    keywords=["closure", "lambda", "capture", "function value", "callback",
              "higher order"],
    items=[
        H("Writing a closure"),
        S("""import std::io

fn main() -> i64 {
    let add = ||(a: i64, b: i64) -> i64 { a + b }
    let negate = ||(n: i64) -> i64 { 0 - n }
    let greet = ||(name: String) -> String { "hi " + name }
    let tick = ||() { io::println("tick") }

    io::println(add(20, 22))
    io::println(negate(5))
    io::println(greet("there"))
    tick()
    0
}""", mode="run", title="Closures of several shapes"),

        H("Leaving the types out"),
        P("Where the closure is going already says what it takes, it need not "
          "be said again: a parameter may be written as a bare name, and the "
          "result follows from the body."),
        S("""import std::io
import std::iter
import std::collections::vector

fn applyTwice(f: @function(i64) -> i64, n: i64) -> i64 { f(f(n)) }

fn main() -> i64 {
    // The parameter's type comes from what `applyTwice` says it takes.
    io::println(applyTwice(||(n) { n + 5 }, 2))

    // And from an annotated binding.
    let double: @function(i64) -> i64 = ||(n) { n * 2 }
    io::println(double(21))

    // And through a call still being inferred: `map` says what the closure
    // is handed while what it hands back is the thing being worked out.
    for n in vec![1, 2, 3].map(||(n) { n * 100 }) {
        io::println(n)
    }
    0
}""", mode="run", title="Written short"),
        P("Nothing about this is special to a particular function. Any place "
          "with a written-down function type — an argument, an annotated "
          "binding, a declared result — supplies the parameters, and a place "
          "that says nothing does not:"),
        S("""fn main() -> i64 {
    let orphan = ||(n) { n + 1 }
    0
}""", mode="diag", title="Nothing here says what `n` is"),
        N("The types may always be written, and mixing the two is fine: "
          "`||(n: i64) { n + 1 }` leaves only the result to be worked out.",
          label="Still allowed"),

        H("Captures"),
        P("A closure copies whatever it uses from the enclosing scope at the "
          "moment it is created. Reference-counted captures are retained for as "
          "long as the closure lives, so the closure can outlive the block that "
          "made it."),
        S("""import std::io

fn makeAdder(amount: i64) -> @function(i64) -> i64 {
    ||(n: i64) -> i64 { n + amount }      // `amount` is captured by value
}

fn main() -> i64 {
    let addTen = makeAdder(10)
    io::println(addTen(5))

    var scale = 3
    let triple = ||(n: i64) -> i64 { n * scale }
    scale = 100                            // too late: the copy was taken
    io::println(triple(7))

    let prefix = "value: "
    let show = ||(n: i64) -> String { prefix + n.$str() }
    io::println(show(42))
    0
}""", mode="run", title="Capture happens at creation"),
        N("Because captures are copies, a closure never observes a later change "
          "to the variable it captured. That also means a closure can be "
          "returned safely.", label="By value, always"),

        P("Captures are **copied** when the closure is made — writing `move` "
          "in front says so, and changes nothing. What the closure holds is "
          "its own; the enclosing scope carries on independently."),
        S("""import std::io

fn main() -> i64 {
    var n = 1
    let copied = move ||() -> i64 { n }
    n = 99
    io::println(copied().$str())     // still 1
    0
}""", mode="run", title="`move` names what already happens"),
        P("Which means assigning to a captured name changes only the "
          "closure's copy. That is easy to write by accident and impossible "
          "to notice at run time, so the compiler says so."),
        S("""fn main() -> i64 {
    var total = 0
    let add = ||(v: i64) -> () { total += v }
    add(5)
    total
}""", mode="run", title="Assigning to a capture is a copy"),
        P("To share one value, capture something that *is* shared: a class is "
          "a reference, so every copy of it names the same object. "
          "`mem::Handle<T>` is that, for a single value."),
        S("""import std::io
import std::mem

fn main() -> i64 {
    let total = mem::of(0)
    let add = ||(v: i64) -> () { *total += v }
    add(5)
    add(7)
    io::println((*total).$str())
    0
}""", mode="run", title="Sharing one value with a closure"),

        H("Passing functions around"),
        P("A named function converts to a function value automatically, so it "
          "can be passed wherever a closure can."),
        S("""import std::io

fn double(n: i64) -> i64 { n * 2 }
fn square(n: i64) -> i64 { n * n }

fn applyAll(f: @function(i64) -> i64, values: [4:i64]) -> String {
    var out = ""
    for v in values { out += f(v).$str() + " " }
    out
}

fn compose(f: @function(i64) -> i64, g: @function(i64) -> i64,
           value: i64) -> i64 {
    f(g(value))
}

fn main() -> i64 {
    let values: [4:i64] = [1, 2, 3, 4]
    io::println(applyAll(double, values))
    io::println(applyAll(square, values))
    io::println(applyAll(||(n: i64) -> i64 { n + 100 }, values))
    io::println(compose(double, square, 3))
    0
}""", mode="run", title="Named functions and closures interchangeably"),

        H("Closures over reference types"),
        P("A closure that captures a class or a `String` keeps it alive. The "
          "captured references are released when the closure itself is."),
        S("""import std::io

class Counter {
    pub total: i64
    fn init(self) { self.total = 0 }
    fn deinit(self) { io::println("  counter released") }
}

fn main() -> i64 {
    io::println("making the closure")
    let bump = {
        let shared = Counter()
        ||(amount: i64) -> i64 {
            shared.total += amount
            shared.total
        }
    }
    io::println(bump(3))
    io::println(bump(4))
    io::println("dropping the closure")
    0
}""", mode="run", title="A captured class outlives its block"),
    ]))

# ===========================================================================
# 11. Structs
# ===========================================================================
SECTIONS.append(Sec(
    "structs", "data types", "Structs",
    "A struct is a value type: assigning one copies it. Fields are private "
    "unless marked `pub`.",
    keywords=["struct", "field", "pub", "default", "literal", "update syntax",
              "method", "value type"],
    items=[
        H("Declaring and building"),
        S("""import std::io

struct Point {
    pub x: f64
    pub y: f64
}

struct Config {
    pub name: String
    pub retries: i64 = 3          // a field default
    pub verbose: bool = false
}

fn main() -> i64 {
    let origin = Point { x: 0.0, y: 0.0 }
    let p = Point { x: 3.0, y: 4.0 }

    let quiet = Config { name: "quiet" }               // defaults fill in
    let loud = Config { name: "loud", verbose: true }

    io::println(p.x.$str() + "," + p.y.$str())
    io::println(origin.x)
    io::println(quiet.name + " " + quiet.retries.$str() + " " + quiet.verbose.$str())
    io::println(loud.verbose)
    0
}""", mode="run", title="Fields, defaults and literals"),

        H("Update syntax"),
        P("`..other` fills in every field the literal does not mention, taking "
          "them from another value of the same type."),
        S("""import std::io

struct Options {
    pub width: i64
    pub height: i64
    pub label: String
}

fn main() -> i64 {
    let base = Options { width: 80, height: 24, label: "base" }
    let wide = Options { width: 120, ..base }
    let renamed = Options { label: "renamed", ..base }

    io::println(wide.width.$str() + "x" + wide.height.$str() + " " + wide.label)
    io::println(renamed.width.$str() + "x" + renamed.height.$str() + " " + renamed.label)
    0
}""", mode="run", title="Deriving one value from another"),
        S("""struct Point { pub x: f64, pub y: f64 }

fn main() -> i64 {
    let p = Point { x: 1.0 }
    0
}""", mode="diag", title="Every field needs a value"),

        H("Methods"),
        S("""import std::io

struct Rect { width: f64, height: f64 }

extend Rect {
    pub fn area(&self) -> f64 { self.width * self.height }
    pub fn scaled(&self, by: f64) -> Rect {
        Rect { width: self.width * by, height: self.height * by }
    }
    pub fn widen(&var self, by: f64) { (*self).width += by }
}

fn main() -> i64 {
    var r = Rect { width: 3.0, height: 4.0 }
    io::println(r.area().$str())
    io::println(r.scaled(2.0).area().$str())
    r.widen(1.0)
    io::println(r.area().$str())
    0
}""", mode="run", title="Reading, deriving, and changing in place"),
        P("Methods live in the struct body, or in an `extend` block, or in a "
          "`bind` block. A method with `&self` borrows the receiver; one with "
          "`self` takes a copy."),
        S("""import std::io

struct Rect {
    pub width: f64
    pub height: f64

    pub fn area(&self) -> f64 { self.width * self.height }
    pub fn scaled(&self, k: f64) -> Rect {
        Rect { width: self.width * k, height: self.height * k }
    }
    pub fn grow(&var self, by: f64) {
        self.width += by
        self.height += by
    }
}

fn main() -> i64 {
    let r = Rect { width: 3.0, height: 4.0 }
    io::println(r.area())
    io::println(r.scaled(2.0).area())

    var growable = Rect { width: 1.0, height: 1.0 }
    growable.grow(2.0)
    io::println(growable.area())
    0
}""", mode="run", title="Reading and mutating methods"),

        H("Structs are copied"),
        S("""import std::io

struct Counter { pub n: i64 }

fn main() -> i64 {
    var a = Counter { n: 1 }
    var b = a               // a copy, not a second name for the same value
    b.n = 99
    io::println(a.n.$str() + " " + b.n.$str())
    0
}""", mode="run", title="Assignment copies a struct"),
        N("A struct holding a `String` or a class still copies, but the copy "
          "shares the referenced object and takes a reference of its own. See "
          "**Memory**.", label="Structs with references"),

        H("Privacy"),
        P("A field with no `pub` is visible inside its own module and nowhere "
          "else. The same goes for methods."),
        S("""pub struct Token {
    pub text: String        // visible to importers
    offset: i64             // module-private

    pub fn length(&self) -> i64 { self.text.$length() }
    fn internalOffset(&self) -> i64 { self.offset }
}"""),

        H("Destroying a value"),
        P("A struct may declare a `deinit`, and it runs when the value it "
          "lives in is destroyed. That is what lets a value own something "
          "reference counting cannot see — a file descriptor, a lock, a "
          "handle from a C library — rather than only memory. It may be "
          "written in the body, in an `extend`, or supplied by a `bind`; all "
          "three are the same method to the compiler."),
        S("""import std::io

struct Handle { pub id: i64 }

extend Handle {
    // `&var self`, not `self`: taking it by value would run on a copy and
    // leave the original holding what it was meant to release.
    fn deinit(&var self) {
        if self.id >= 0 {
            io::println("closing " + self.id.$str())
            self.id = -1
        }
    }
}

fn main() -> i64 {
    io::println("before")
    {
        let h = Handle { id: 1 }
        io::println("using " + h.id.$str())
    }
    io::println("after")
    0
}""", mode="run", title="A `deinit` in an `extend`"),
        P("It runs from wherever the value is: a local at the end of its "
          "scope, a statement whose value is thrown away, and anything "
          "*containing* one — a field of a struct or a class, an element of "
          "an array, a member of a tuple."),
        S("""import std::io

struct Handle { pub id: i64 }
extend Handle { fn deinit(&self) { io::println("closing " + self.id.$str()) } }

struct Pair { pub a: Handle, pub b: Handle }
class Owner {
    h: Handle
    fn init(self) { self.h = Handle { id: 99 } }
}

fn main() -> i64 {
    { let p = Pair { a: Handle { id: 10 }, b: Handle { id: 11 } } }
    { let arr: [2:Handle] = [Handle { id: 20 }, Handle { id: 21 }] }
    { let o = Owner() }
    0
}""", mode="run", title="Reached through whatever holds it"),

        H("Handing an owning value on"),
        P("Only one thing may own a resource, so giving one away is a *move*: "
          "returning it, passing it by value, or storing it somewhere that "
          "outlives the binding. The binding it came from stops owning it, "
          "and stops being usable."),
        S("""import std::io

struct Handle { pub id: i64 }
extend Handle { fn deinit(&self) { io::println("closing " + self.id.$str()) } }

/// The local is returned, so this scope stops owning it: one `closing`, not
/// two.
fn make(n: i64) -> Handle {
    let h = Handle { id: n }
    io::println("made " + n.$str())
    h
}

fn consume(h: Handle) { io::println("consumed " + h.id.$str()) }
fn borrow(h: &Handle) { io::println("borrowed " + h.id.$str()) }

fn main() -> i64 {
    { let x = make(5); io::println("have " + x.id.$str()) }
    // Passing by value hands it over; the callee destroys it.
    { let a = Handle { id: 2 }; consume(a) }
    // Borrowing takes nothing, so the caller still destroys it.
    { let b = Handle { id: 3 }; borrow(&b) }
    0
}""", mode="run", title="Moved, or borrowed"),
        S("""struct Handle { id: i64 }
extend Handle { fn deinit(&self) { } }

fn consume(h: Handle) { }

fn main() -> i64 {
    let a = Handle { id: 2 }
    consume(a)
    a.id
}""", mode="diag", title="Using what has been handed away"),
        N("A move is tracked per binding, and the check is textual rather "
          "than a walk of every path. What that misses — a move inside a loop "
          "that runs twice — is safe at run time: the binding carries a flag "
          "saying whether it still owns anything, so a destructor never runs "
          "twice.", label="What the check covers"),

        H("`@resource`: a field that has to be released"),
        P("A field marked `@resource` holds something the compiler cannot "
          "release on its own. Saying so makes the type's `deinit` "
          "responsible for it: at `--safety full` a `deinit` that never "
          "mentions the field is an error, and so is having no `deinit` at "
          "all. Below `full` both are warnings."),
        S("""import std::io

extern "C" { fn close(fd: i32) -> i32 }

pub struct Socket {
    @resource fd: i32 = -1,
    pub port: i32 = 0
}

extend Socket {
    @safe("the descriptor is ours, and the flag stops a second close")
    fn deinit(&var self) {
        if self.fd >= 0 {
            io::println("close(" + self.fd.$str() + ")")
            close(self.fd)
            self.fd = -1
        }
    }
}

fn main() -> i64 {
    { var s = Socket { fd: 3, port: 80 }; io::println("serving on " + s.port.$str()) }
    0
}""", mode="run", title="A descriptor that closes itself"),
        S("""import std::io

struct Socket {
    @resource fd: i32,
    port: i32
}

extend Socket {
    fn deinit(&self) { io::println("bye") }
}

fn main() -> i64 { let s = Socket { fd: 3, port: 80 }; 0 }""",
          mode="diag", title="A `@resource` the `deinit` forgets"),
        N("An enum may declare a `deinit` in exactly the same way, and a "
          "`mark` may require one — `std::net`'s `TcpStream` and "
          "`TcpListener` are both structs that own a descriptor this way."),

        H("One struct extending another"),
        P("`struct Derived : Base` puts the parent's fields at the **front** "
          "of the child's. So a `Derived` is a `Base` with more on the end, "
          "and the bytes a `Base` occupies are the first bytes of one: the "
          "parent's methods work on the child, and the child reads as a "
          "`Base` wherever one is wanted."),
        S("""import std::io

struct Base { pub id: i64 = 0, pub name: String = "" }

extend Base {
    fn label(&self) -> String { self.name + "#" + self.id.$str() }
    fn bump(&var self) { self.id += 1 }
}

struct Derived : Base { pub extra: i64 = 0 }
struct Deeper : Derived { pub more: String = "" }

fn describe(b: Base) -> String { b.label() }

fn main() -> i64 {
    var d = Derived { id: 7, name: "seven", extra: 3 }
    io::println(d.label())          // the parent's method
    d.bump()                        // including one that writes
    io::println(describe(d.$clone()))   // read as a `Base`

    let deep = Deeper { id: 1, name: "one", extra: 2, more: "yes" }
    io::println(deep.label())
    0
}""", mode="run", title="Fields first, methods along with them"),
        T(["", "Struct", "Class"],
          [["written", "`struct D : B`", "`class D : B`"],
           ["fields", "spliced in, the parent's first",
            "kept in the parent, reached through it"],
           ["reading as the parent", "a copy of the prefix",
            "the same object"],
           ["how deep", "as many levels as you like, either way",
            "as many levels as you like, either way"]],
          caption="A value has no indirection to walk, so its parent's fields "
                  "are spliced in once and everything downstream sees one "
                  "flat type."),
        N("A child may add fields, not redefine the parent's, and the parent "
          "may not be generic — its members are spliced in as they are "
          "written, and nothing would say what its parameters were.",
          label="Two limits"),
    ]))

# ===========================================================================
# 12. Enums
# ===========================================================================
SECTIONS.append(Sec(
    "enums", "data types", "Enums",
    "An enum is a value that is exactly one of several shapes. Variants may "
    "carry nothing, a tuple, or named fields.",
    keywords=["enum", "variant", "discriminant", "payload", "tagged union",
              "generic enum"],
    items=[
        H("The three variant shapes"),
        S("""import std::io

// An enum is how a value that is one of several shapes gets a single type.
enum Json {
    Null,
    Bool(bool),
    Number(f64),
    Text(String),
}

fn render(v: Json) -> String {
    match v {
        Json::Null => "null",
        Json::Bool(b) => b.$str(),
        Json::Number(n) => n.$str(),
        Json::Text(t) => "'" + t + "'",
    }
}

fn main() -> i64 {
    io::println(render(Json::Null))
    io::println(render(Json::Bool(true)))
    io::println(render(Json::Number(2.5)))
    io::println(render(Json::Text("hi")))
    0
}""", mode="run", title="One type, several shapes"),
        S("""import std::io

enum Shape {
    Empty,                              // unit: no payload
    Circle(f64),                        // tuple: positional payload
    Rect { width: f64, height: f64 },   // struct: named payload
}

fn area(s: Shape) -> f64 {
    match s {
        Shape::Empty => 0.0,
        Shape::Circle(r) => 3.14159265 * r * r,
        Shape::Rect { width, height } => width * height,
    }
}

fn main() -> i64 {
    io::println(area(Shape::Empty))
    io::println(area(Shape::Circle(2.0)))
    io::println(area(Shape::Rect { width: 3.0, height: 4.0 }))
    0
}""", mode="run", title="Unit, tuple and struct variants"),

        H("Explicit discriminants"),
        P("A variant with no payload may be given a number. Such an enum "
          "converts to an integer with `as`; the numbering continues from the "
          "last value given."),
        S("""import std::io

enum Status {
    Ok = 200,
    Created,            // 201
    NotFound = 404,
    Teapot = 418,
}

fn main() -> i64 {
    io::println(Status::Ok as i64)
    io::println(Status::Created as i64)
    io::println(Status::NotFound as i64)
    io::println(Status::Teapot as i64)
    io::println(Status::Ok == Status::Ok)
    0
}""", mode="run", title="Enums with wire values"),

        H("Methods on an enum"),
        S("""import std::io

enum Direction {
    North, East, South, West,

    pub fn turnRight(&self) -> Direction {
        match self {
            Direction::North => Direction::East,
            Direction::East => Direction::South,
            Direction::South => Direction::West,
            Direction::West => Direction::North,
        }
    }

    pub fn name(&self) -> String {
        match self {
            Direction::North => "north",
            Direction::East => "east",
            Direction::South => "south",
            Direction::West => "west",
        }
    }
}

fn main() -> i64 {
    var d = Direction::North
    var trace = ""
    for i in 0..5 {
        trace += d.name() + " "
        d = d.turnRight()
    }
    io::println(trace)
    0
}""", mode="run", title="A state machine in an enum"),

        H("Generic enums"),
        P("An enum may take type parameters. Each set of arguments produces its "
          "own instantiation, and the variants carry the substituted types."),
        S("""import std::io

pub enum Tree<T> {
    Leaf,
    Node(T),

    pub fn value(&self, fallback: T) -> T {
        match self {
            Tree::Node(v) => v,
            Tree::Leaf => fallback,
        }
    }
}

fn main() -> i64 {
    let numeric = Tree<i64>::Node(7)
    let empty = Tree<i64>::Leaf
    let textual = Tree<String>::Node("hello")

    io::println(numeric.value(0))
    io::println(empty.value(-1))
    io::println(textual.value("nothing"))
    0
}""", mode="run", title="One enum, several instantiations"),
        N("Write the arguments as `Tree<i64>::Node(...)` or "
          "`Tree::<i64>::Node(...)` — both parse. When an argument determines "
          "them, as in `Tree::Node(7)` used where a `Tree<i64>` is wanted, they "
          "are inferred.", label="Spelling the arguments"),

        H("Recursive shapes need a class"),
        P("An enum is a value, so a variant cannot contain the enum itself — "
          "that would have no finite size. Put the recursive part behind a "
          "class."),
        S("""enum List {
    Nil,
    Cons(i64, List),
}

fn main() -> i64 { 0 }""", mode="diag", title="A directly recursive enum"),
        S("""import std::io

class Cell {
    pub head: i64
    pub tail: List
    fn init(self, head: i64, tail: List) {
        self.head = head
        self.tail = tail
    }
}

enum List {
    Nil,
    Cons(Cell),

    pub fn sum(&self) -> i64 {
        match self {
            List::Nil => 0,
            List::Cons(cell) => cell.head + cell.tail.sum(),
        }
    }
}

fn main() -> i64 {
    let list = List::Cons(Cell(1, List::Cons(Cell(2, List::Cons(Cell(3, List::Nil))))))
    io::println(list.sum())
    0
}""", mode="run", title="The same shape, through a class", safety="minimal"),

        H("Variant names are a convenience, not a declaration"),
        P("`Colour::Red` is the name of a variant. A bare `Red` is a "
          "shorthand for it, and a shorthand is all it is: it lives beside "
          "the names a module declares rather than among them. So a variable, "
          "a function or a type may be called `Red` too, and two enums may "
          "each have one."),
        S("""import std::io

enum Car  { Body, Wheel, Door }
enum Ship { Hull, Wheel, Sail }

// A binding may take a variant's name. Neither enum loses anything.
global Wheel: i64 = 42
fn Door() -> i64 { 7 }

fn describe(c: Car) -> String {
    match c {
        // The scrutinee is a `Car`, so a bare name means one of its variants.
        Wheel => "car wheel",
        Body  => "car body",
        Door  => "car door",
    }
}

fn main() -> i64 {
    io::println(Wheel)                    // the global
    io::println(describe(Car::Wheel))     // the variant
    io::println(Door())                   // the function
    if Body == Car::Body { io::println("`Body` is claimed by one enum, so it works bare") }
    0
}""", mode="run", title="Three things called `Wheel`"),
        P("What a shared name costs is that a bare mention of it has to say "
          "which enum it belongs to. The error arrives where the ambiguity "
          "actually is — at the use, not at the declaration — and lists the "
          "candidates."),
        S("""enum Car  { Body, Wheel }
enum Ship { Hull, Wheel }

fn main() -> i64 {
    let c = Wheel
    0
}""", mode="diag", title="A bare name two enums claim"),
        N("A `match` arm is the exception, because the scrutinee's type "
          "already says which enum is meant. `Wheel =>` inside a `match` on a "
          "`Car` is `Car::Wheel`, and needs no qualification."),
        N("`Option` and `Result` are in scope everywhere, so `Some`, `None`, "
          "`Ok` and `Err` are too — but only as the lowest tier. A module "
          "that declares an enum with a `Some` of its own wins outright, and "
          "is not made ambiguous by the prelude's."),

        H("A leading dot: whatever type is wanted here"),
        P("Where the type is already known — an annotation, an argument, a "
          "`match` on a value — saying it again adds nothing. A leading `.` "
          "names something on that type: a variant, a static method, anything "
          "the type owns."),
        S("""import std::io

enum Colour { Red, Green, Blue }
enum Shape { Circle(f64), Rect(f64, f64) }

struct Duration { ms: i64 }
extend Duration {
    fn seconds(n: i64) -> Duration { Duration { ms: n * 1000 } }
    fn zero() -> Duration { Duration { ms: 0 } }
}

fn paint(c: Colour) -> i64 { c as i64 }
fn wait(d: Duration) -> i64 { d.ms }

fn area(s: Shape) -> f64 {
    match s { .Circle(r) => r * r * 3.0, .Rect(w, h) => w * h }
}

fn main() -> i64 {
    let c: Colour = .Blue               // a variant
    let d: Duration = .seconds(5)       // a static method
    let list: [3:Colour] = [.Red, .Green, .Blue]

    io::println(paint(c))
    io::println(wait(.zero()))
    io::println(area(.Rect(2.0, 3.0)))
    io::println(paint(list[1]))
    0
}""", mode="run", title="`.Name`, wherever the type is already said"),
        P("It works in a pattern too, where it also says the name is a "
          "variant rather than a new binding — so `.Purple` on a `Colour` is "
          "a mistake, where a bare `Purple` would quietly have bound the "
          "value."),
        S("""enum Colour { Red, Green, Blue }

fn name(c: Colour) -> String {
    match c { .Red => "red", .Green => "green", .Purple => "?" }
}

fn main() -> i64 { 0 }""", mode="diag", title="No such variant"),
        N("Only `.`, never `::`. A leading `::` reads as a path with an empty "
          "first segment, which several languages spell that way; keeping it "
          "free costs nothing, and one spelling is easier to read than two.",
          label="Why not `::Name`"),

        H("One enum extending another"),
        P("`enum Derived : Base` puts the parent's variants **first**, with "
          "the numbers they had. So every `Base` is a `Derived`, and a "
          "`match` over the child covers the parent's variants by name."),
        S("""import std::io

enum Level { Low, High }
enum Extended : Level { Critical }

fn urgency(e: Extended) -> i64 { e as i64 }

fn main() -> i64 {
    let l: Level = .High
    io::println(urgency(l))          // a `Level` widens into an `Extended`

    let e: Extended = .Critical
    io::println(urgency(e))
    io::println(match e {
        .Low => "low",
        .High => "high",
        .Critical => "critical",
    })
    0
}""", mode="run", title="The parent's variants, and one more"),
        N("A struct goes the other way: a `Derived` struct reads as its "
          "`Base` because the parent's *fields* come first, while a `Base` "
          "enum reads as its `Derived` because the parent's *variants* do. "
          "Both follow from putting the parent first.",
          label="Why the direction differs"),
    ]))

# ===========================================================================
# 13. Classes
# ===========================================================================
SECTIONS.append(Sec(
    "classes", "data types", "Classes",
    "A class is a reference type: assigning one shares the instance. Classes "
    "have single inheritance, virtual methods, and destructors that run "
    "deterministically.",
    keywords=["class", "init", "deinit", "inheritance", "super", "virtual",
              "override", "reference type", "is", "destructor"],
    items=[
        H("Declaring a class"),
        P("`init` is the constructor and `deinit` the destructor; both are "
          "optional. Calling the class runs `init`."),
        S("""import std::io

class Buffer {
    pub name: String
    pub used: i64

    fn init(self, name: String) {
        self.name = name
        self.used = 0
    }

    fn deinit(self) {
        io::println("  releasing " + self.name)
    }

    pub fn write(&self, amount: i64) {
        self.used += amount
    }

    pub fn describe(&self) -> String {
        self.name + " holds " + self.used.$str()
    }
}

fn main() -> i64 {
    let b = Buffer("frame")
    b.write(3)
    b.write(4)
    io::println(b.describe())
    io::println("leaving main")
    0
}""", mode="run", title="Construction, use, destruction"),
        N("A class with no `init` takes no arguments. Field defaults still "
          "apply, and they run before `init` does, so `init` can overwrite "
          "them.", label="No initialiser"),

        H("Classes are shared, not copied"),
        S("""import std::io

class Tally { pub n: i64
    fn init(self) { self.n = 0 } }

struct Count { pub n: i64 }

fn main() -> i64 {
    let a = Tally()
    let b = a                  // the same instance, not a copy
    b.n = 99
    io::println("class:  " + a.n.$str() + " " + b.n.$str())

    var c = Count { n: 1 }
    var d = c                  // a copy
    d.n = 99
    io::println("struct: " + c.n.$str() + " " + d.n.$str())
    0
}""", mode="run", title="Reference semantics versus value semantics"),

        H("Inheritance and `super`"),
        S("""import std::io

class Shape {
    name: String
    fn init(self, name: String) { self.name = name }
    pub fn area(&self) -> f64 { 0.0 }
    pub fn describe(&self) -> String {
        self.name + " of area " + self.area().$str()
    }
}

class Square: Shape {
    side: f64
    fn init(self, side: f64) {
        super.init("square")
        self.side = side
    }
    pub fn area(&self) -> f64 { self.side * self.side }
}

fn main() -> i64 {
    let s = Square(3.0)
    // `describe` is the base's, but the `area` it reaches is the subclass's.
    io::println(s.describe())
    0
}""", mode="run", title="A base method reaching an overridden one"),
        P("A class may name one base class after a colon. A method with the "
          "same name overrides the base version and is dispatched dynamically; "
          "`super.method()` calls the base version directly."),
        S("""import std::io

class Animal {
    pub name: String
    fn init(self, name: String) { self.name = name }
    pub fn speak(&self) -> String { "..." }
    pub fn describe(&self) -> String { self.name + " says " + self.speak() }
}

class Dog : Animal {
    fn init(self, name: String) { super.init(name) }
    pub fn speak(&self) -> String { "woof" }
}

class Puppy : Dog {
    fn init(self, name: String) { super.init(name) }
    pub fn speak(&self) -> String { "yip (" + super.speak() + ")" }
}

fn announce(a: Animal) -> String { a.describe() }

fn main() -> i64 {
    io::println(announce(Animal("Generic")))
    io::println(announce(Dog("Rex")))
    io::println(announce(Puppy("Pip")))
    0
}""", mode="run", title="Overriding, and reaching the base implementation"),
        N("`describe` is declared once on `Animal` and never overridden, yet it "
          "calls the *derived* `speak`. That is dynamic dispatch: the method is "
          "chosen from the instance's real type, not the static one.",
          label="Why the base method changes behaviour"),

        H("Destruction order"),
        P("A destructor runs when the last reference goes away. Within one "
          "instance the order is: the class's own `deinit`, then its own "
          "fields, then the base class does the same."),
        S("""import std::io

class Base {
    pub tag: String
    fn init(self, tag: String) { self.tag = tag }
    fn deinit(self) { io::println("  Base.deinit " + self.tag) }
}

class Derived : Base {
    fn init(self, tag: String) { super.init(tag) }
    fn deinit(self) { io::println("  Derived.deinit " + self.tag) }
}

fn main() -> i64 {
    io::println("scope opens")
    {
        let first = Derived("one")
        let second = Derived("two")
        io::println("scope closing")
    }
    io::println("scope closed")
    0
}""", mode="run", title="Derived first, then base; locals in reverse"),

        H("Asking what a value really is"),
        S("""import std::io

class Shape { fn init(self) {} pub fn sides(&self) -> i64 { 0 } }
class Triangle : Shape { fn init(self) { super.init() }
    pub fn sides(&self) -> i64 { 3 } }
class Square : Shape { fn init(self) { super.init() }
    pub fn sides(&self) -> i64 { 4 } }

fn label(s: Shape) -> String {
    if s is Triangle { return "triangle" }
    if s is Square { return "square" }
    "shape"
}

fn main() -> i64 {
    let shapes: [3:Shape] = [Triangle(), Square(), Shape()]
    var out = ""
    for s in shapes {
        out += label(s) + "/" + s.sides().$str() + " "
    }
    io::println(out)
    0
}""", mode="run", title="`is` and a heterogeneous array of subclasses"),

        H("Static methods"),
        P("A method without a `self` parameter belongs to the type rather than "
          "an instance, and is called through the type name."),
        S("""import std::io

class Temperature {
    pub celsius: f64
    fn init(self, celsius: f64) { self.celsius = celsius }

    pub fn fromFahrenheit(f: f64) -> Temperature {
        Temperature((f - 32.0) / 1.8)
    }
    pub fn freezing() -> Temperature { Temperature(0.0) }
}

fn main() -> i64 {
    io::println(Temperature::fromFahrenheit(212.0).celsius)
    io::println(Temperature::freezing().celsius)
    0
}""", mode="run", title="Constructors that are not `init`"),
    ]))

# ===========================================================================
# 14. Marks
# ===========================================================================
SECTIONS.append(Sec(
    "marks", "abstraction", "Marks",
    "A mark is Rune's trait: a set of methods a type can promise to provide. "
    "Marks bind to any type, including the builtin ones.",
    keywords=["mark", "trait", "bind", "default method", "super-mark", "extend",
              "interface", "conformance", "protocol", "associated type",
              "Self::Item", "type Item", "where clause", "requirement",
              "some", "opaque", "dyn", "auto", "automatic mark", "@auto",
              "@never", "autotrait", "Clone", "Send", "Sync"],
    items=[
        H("Declaring a mark"),
        P("A method with no body is a requirement. A method with a body is a "
          "default that a binding may accept or replace."),
        S("""import std::io

mark Show {
    fn show(&self) -> String                        // required
    fn shout(&self) -> String { self.show() + "!" } // default
}

struct Point { x: i64, y: i64 }

bind Show to Point {
    fn show(&self) -> String { "(" + self.x.$str() + "," + self.y.$str() + ")" }
}

struct Loud { text: String }

bind Show to Loud {
    fn show(&self) -> String { self.text }
    fn shout(&self) -> String { self.text.$repeat(3) }   // replaces the default
}

fn main() -> i64 {
    io::println(Point { x: 1, y: 2 }.show())
    io::println(Point { x: 1, y: 2 }.shout())
    io::println(Loud { text: "ha" }.shout())
    0
}""", mode="run", title="Requirements and defaults"),
        N("Each binding gets its **own copy** of the mark's defaults, so inside "
          "a default `Self` is the concrete type and a call to a sibling method "
          "reaches that type's implementation.", label="How defaults resolve"),

        H("Unimplemented requirements are caught"),
        S("""mark Show {
    fn show(&self) -> String
    fn describe(&self) -> String
}

struct Point { x: i64 }

bind Show to Point {
    fn show(&self) -> String { "point" }
}

fn main() -> i64 { 0 }""", mode="diag", title="A missing requirement"),

        H("Super-marks"),
        P("A mark may require other marks. Binding it then requires those too."),
        S("""import std::io

mark Named {
    fn name(&self) -> String
}

mark Measured: Named {              // requires Named as well
    fn size(&self) -> f64
    fn summary(&self) -> String {
        self.name() + " is " + self.size().$str()
    }
}

struct Crate { label: String, volume: f64 }

bind Named to Crate {
    fn name(&self) -> String { self.label }
}

bind Measured to Crate {
    fn size(&self) -> f64 { self.volume }
}

fn main() -> i64 {
    io::println(Crate { label: "box", volume: 2.5 }.summary())
    0
}""", mode="run", title="A mark that builds on another"),
        S("""mark Named { fn name(&self) -> String }
mark Measured: Named { fn size(&self) -> f64 }

struct Crate { volume: f64 }

bind Measured to Crate {
    fn size(&self) -> f64 { self.volume }
}

fn main() -> i64 { 0 }""", mode="diag", title="The super-mark is not satisfied"),

        H("Binding to builtin types"),
        P("A mark may be bound to any type at all, including `i64` and `String`. "
          "That is exactly how `io::Display` makes `println` work for "
          "everything. On a builtin, `&self` passes the value, since there is "
          "nothing to look inside."),
        S("""import std::io

mark Doubled {
    fn doubled(&self) -> String
}

bind Doubled to i64 {
    fn doubled(&self) -> String { (self * 2).$str() }
}

bind Doubled to f64 {
    fn doubled(&self) -> String { (self * 2.0).$str() }
}

bind Doubled to String {
    fn doubled(&self) -> String { self + self }
}

bind Doubled to bool {
    fn doubled(&self) -> String { self.$str() + self.$str() }
}

fn show<T: Doubled>(v: T) -> String { v.doubled() }

fn main() -> i64 {
    io::println(show(21))
    io::println(show(1.25))
    io::println(show("ab"))
    io::println(show(true))
    0
}""", mode="run", title="Marks on `i64`, `f64`, `String` and `bool`"),

        H("Conditional bindings"),
        P("A generic binding may carry a `where` clause. The binding then "
          "applies only to the instantiations that satisfy it."),
        S("""import std::io

mark Show { fn show(&self) -> String }

struct Wrapper<T> { value: T }

bind Show to i64 { fn show(&self) -> String { self.$str() } }

bind<T> Show to Wrapper<T> where T: Show {
    fn show(&self) -> String { "Wrapper(" + self.value.show() + ")" }
}

struct Opaque { n: i64 }

fn main() -> i64 {
    io::println(Wrapper<i64> { value: 7 }.show())

    // Wrapper<Opaque> exists, but is not Show, because Opaque is not:
    let hidden = Wrapper<Opaque> { value: Opaque { n: 1 } }
    io::println(hidden.value.n)
    0
}""", mode="run", title="`where` decides whether a binding applies"),

        H("`Self`: requirements that build"),
        P("A requirement with no `self` parameter is a static one, and `Self` "
          "in its signature stands for whichever type implements it. That is "
          "how a mark describes a constructor."),
        S("""import std::io

struct Sheep { naked: bool, name: String }

mark Animal {
    fn new(name: String) -> Self      // static: no `self`

    fn name(&self) -> String
    fn noise(&self) -> String
    fn speak(&self) { io::println(self.name() + " says " + self.noise()) }
}

bind Animal to Sheep {
    fn new(name: String) -> Self { Sheep { naked: false, name: name } }
    fn name(&self) -> String { self.name }
    fn noise(&self) -> String { if self.naked { "baaaa!" } else { "baaaa?" } }
}

fn main() -> i64 {
    // The annotation is what picks the implementation.
    let dolly: Sheep = Animal::new("dolly")
    dolly.speak()
    0
}""", mode="run", title="A mark that constructs"),
        P("A static requirement has no receiver, so nothing about the call "
          "says which implementation to run. Rune takes it from the type the "
          "result flows into — an annotation, a parameter, or a return type. "
          "With nothing to go on, it asks."),
        S("""struct Sheep { naked: bool, name: String }

mark Animal {
    fn new(name: String) -> Self
    fn noise(&self) -> String
}

bind Animal to Sheep {
    fn new(name: String) -> Self { Sheep { naked: false, name: name } }
    fn noise(&self) -> String { "baaaa" }
}

fn main() -> i64 {
    let mystery = Animal::new("dolly")
    0
}""", mode="diag", title="Nothing to infer from"),
        P("`Self` works as a parameter type too, which is what lets a mark "
          "describe an operation over its own type. Combined with a generic "
          "bound, one function then serves every implementation — including "
          "the builtin ones."),
        S("""import std::io

mark Monoid {
    fn zero() -> Self
    fn combine(&self, other: Self) -> Self
}

struct V2 { x: i64, y: i64 }

bind Monoid to V2 {
    fn zero() -> Self { V2 { x: 0, y: 0 } }
    fn combine(&self, other: Self) -> Self {
        V2 { x: self.x + other.x, y: self.y + other.y }
    }
}

bind Monoid to i64 {
    fn zero() -> Self { 0 }
    fn combine(&self, other: Self) -> Self { self + other }
}

bind Monoid to String {
    fn zero() -> Self { "" }
    fn combine(&self, other: Self) -> Self { self + other }
}

fn total<T: Monoid>(values: [T]) -> T {
    var acc: T = Monoid::zero()
    for v in values { acc = acc.combine(v) }
    acc
}

fn main() -> i64 {
    let ns: [4:i64] = [1, 2, 3, 4]
    let words: [3:String] = ["a", "b", "c"]
    let vs: [2:V2] = [V2 { x: 1, y: 2 }, V2 { x: 10, y: 20 }]
    io::println(total(ns).$str())
    io::println(total(words))
    let sum = total(vs)
    io::println(sum.x.$str() + "," + sum.y.$str())
    0
}""", mode="run", title="`Self` on both sides"),
        P("An implementation has to match the requirement once `Self` is read "
          "as the implementing type."),
        S("""struct Metre { v: i64 }
struct Foot { v: i64 }

mark Zeroed { fn zero() -> Self }

bind Zeroed to Metre {
    fn zero() -> Foot { Foot { v: 0 } }
}

fn main() -> i64 { 0 }""", mode="diag", title="A `Self` that does not line up"),
        N("A static requirement is not reachable through a `dyn Mark` value. A "
          "mark object carries one implementation chosen at run time, and a "
          "call with no receiver has nothing to choose from — call it on a "
          "concrete type instead.", label="Not through `dyn`", tone="warn"),

        H("When a type answers for itself"),
        P("A type's own methods — its body and its `extend` blocks — always "
          "win `value.name()`. A `bind` fills in what the type does not "
          "already have; it never displaces it, and says so if you write one "
          "that would."),
        S("""import std::io

struct Ticket { number: i64 }

extend Ticket {
    pub fn label(&self) -> String { "#" + self.number.$str() }
}

mark Formal {
    fn label(&self) -> String
    fn heading(&self) -> String { "-- " + self.label() + " --" }
}

bind Formal to Ticket {
    fn label(&self) -> String { "Ticket number " + self.number.$str() }
}

fn main() -> i64 {
    let t = Ticket { number: 42 }
    io::println(t.label())             // the type's own
    io::println(t::Formal.label())     // the mark's, asked for by name
    io::println(t.heading())           // a default, calling the mark's `label`
    0
}""", mode="run", title="Both are reachable"),
        T(["Written", "Finds"],
          [["`value.name()`", "the type's own method, or a mark's if it has "
            "none"],
           ["`value::Mark.name()`", "the method `Mark` binds for this type"],
           ["`self.name()` inside a bound method", "that mark's own `name`"],
           ["`Mark::name(...)`", "the static requirement, for the type in "
            "context"]],
          caption="`value::Mark.name()` takes a plain name on the left — bind "
                  "an expression to one first."),
        P("A default body always calls the mark's own requirements, not the "
          "type's lookalikes. It was written against the mark, so that is what "
          "it gets — which is why `heading` above reads *Ticket number 42* "
          "rather than *#42*."),
        P("A field and a method may share a name. The syntax settles it: "
          "`self.name` is the field, `self.name()` the method — unless the "
          "field itself holds something callable, which then wins."),
        S("""import std::io

struct Sheep { name: String }

mark Named { fn name(&self) -> String }

bind Named to Sheep {
    // `self.name` reads the field; the mark's requirement is this method.
    fn name(&self) -> String { "the sheep called " + self.name }
}

fn main() -> i64 {
    let s = Sheep { name: "dolly" }
    io::println(s.name)      // the field
    io::println(s.name())    // the method
    0
}""", mode="run", title="A field and a method, one name"),

        H("One name, several parameter lists"),
        P("A `fn` may not be declared twice. A `bind` may: the same mark may "
          "be bound to one type more than once, and the versions are told "
          "apart by what they take. Together they are an **overload set**, "
          "and the call site picks by the arguments it hands over. This is "
          "the only overloading in the language, and a `bind` is the only "
          "place to write it."),
        S("""import std::io

struct Cart { pub total: i64 }

mark Add { fn add(&var self, item: String) }

// The version whose signature is the one the mark asked for is the one that
// answers the requirement. The others are extra ways to call the name.
bind Add to Cart {
    fn add(&var self, item: String) { io::println("item " + item) }
}
bind Add to Cart {
    fn add(&var self, cents: i64) { self.total = self.total + cents }
}
bind Add to Cart {
    fn add(&var self, item: String, cents: i64) {
        io::println(item + " " + cents.$str())
        self.total = self.total + cents
    }
}

fn main() -> i64 {
    var c = Cart { total: 0 }
    c.add("apple")
    c.add(150)
    c.add("pear", 200)
    io::println(c.total.$str())
    0
}""", mode="run", title="Three ways to call one name"),
        P("Selection is by type, not by conversion first: an exact match "
          "beats one that would need a widening, and a call that two versions "
          "accept equally well is reported rather than decided by declaration "
          "order. So is a call no version accepts — with every candidate "
          "listed."),
        P("Everything else about the call works the way it always does. A "
          "labelled argument goes to the parameter of that name, the "
          "positional ones fill what is left in order, and a parameter "
          "nothing filled has to have brought its own default — so a version "
          "with a defaulted tail is a candidate for the shorter call too."),
        S("""import std::io

struct Line { pub width: i64 }
mark Draw { fn draw(&self, fill: Character) }

bind Draw to Line {
    fn draw(&self, fill: Character) {
        var out = ""
        for _ in 0..self.width { out += fill }
        io::println(out)
    }
}
bind Draw to Line {
    fn draw(&self, label: String, pad: Character = '.') {
        var out = label
        while out.$length() < self.width { out += pad }
        io::println(out)
    }
}

fn main() -> i64 {
    let l = Line { width: 8 }
    l.draw('-')                        // the Character version
    l.draw("name")                     // the String one, `pad` defaulted
    l.draw("name", '_')
    l.draw(pad: '*', label: "name")    // labels, in either order
    0
}""", mode="run", title="Labels and defaults, as usual"),
        S("""import std::io

struct Cart { pub total: i64 }
mark Add { fn add(&var self, cents: i32) }

bind Add to Cart { fn add(&var self, cents: i32) { } }
bind Add to Cart { fn add(&var self, cents: i64) { } }

fn main() -> i64 {
    var c = Cart { total: 0 }
    small: i16 = 3
    c.add(small)          // widens to both, equally
    0
}""", mode="diag", title="Two that fit equally well"),
        N("Two bindings that take the *same* things are still two answers to "
          "one question, and are reported as equally specific. Specificity "
          "comes first: a written-out target beats a parameterised one "
          "whatever either of them takes, so a narrower binding replaces a "
          "wider one rather than overloading against it.",
          label="What is still a clash"),
        P("Only one version answers the mark's requirement — the one whose "
          "signature matches it — and that is what a `dyn Mark` value calls "
          "and what `value::Mark.name()` reaches when nothing distinguishes "
          "the call. The rest are reachable on the concrete type, and through "
          "`value::Mark.name(...)` when the arguments say which."),

        H("Associated types"),
        P("A mark may declare a type each binding chooses, rather than fixing "
          "it up front. `type Item` inside the mark, `type Item = i64` inside "
          "the bind, and `Self::Item` wherever the requirement needs to name "
          "it."),
        S("""import std::io

mark Container {
    type Item
    fn count(&self) -> i64
    fn first(&self) -> Self::Item
}

struct Bag { values: [3:i64] }
struct Names { values: [2:String] }

bind Container to Bag {
    type Item = i64
    fn count(&self) -> i64 { 3 }
    fn first(&self) -> Self::Item { self.values[0] }
}

bind Container to Names {
    type Item = String
    fn count(&self) -> i64 { 2 }
    fn first(&self) -> Self::Item { self.values[0] }
}

fn howMany<T: Container>(c: T) -> i64 { c.count() }

fn main() -> i64 {
    let b = Bag { values: [7, 8, 9] }
    let n = Names { values: ["ada", "grace"] }
    io::println(b.first().$str())
    io::println(n.first())
    io::println(howMany(b).$str() + " " + howMany(n).$str())
    0
}""", mode="run", title="One mark, two element types"),
        P("A requirement can constrain what may be chosen. `type Item: "
          "io::Display` means every binding's `Item` has to be printable, and "
          "the bind is where that is checked."),
        S("""import std::io

struct Opaque { v: i64 }

mark Container {
    type Item: io::Display
    fn first(&self) -> Self::Item
}

struct Bad { v: Opaque }

bind Container to Bad {
    type Item = Opaque
    fn first(&self) -> Self::Item { self.v }
}

fn main() -> i64 { 0 }""", mode="diag", title="A choice that breaks its bound"),
        S("""mark Container {
    type Item
    fn first(&self) -> Self::Item
}

struct Bag { v: i64 }

bind Container to Bag {
    fn first(&self) -> i64 { self.v }
}

fn main() -> i64 { 0 }""", mode="diag", title="Forgetting to choose one"),

        H("`extend`: methods without a mark"),
        P("`extend` adds inherent methods to a type you did not declare, or to "
          "a builtin. There is no mark involved."),
        S("""import std::io

struct Point { x: f64, y: f64 }

extend Point {
    fn magnitude(&self) -> f64 {
        squareRoot(self.x * self.x + self.y * self.y)
    }
    fn scaled(&self, k: f64) -> Point {
        Point { x: self.x * k, y: self.y * k }
    }
}

extend i64 {
    fn squared(&self) -> i64 { self * self }
    fn isEven(&self) -> bool { self % 2 == 0 }
}

extend String {
    fn shout(&self) -> String { self + "!" }
}

extern "C" { fn sqrt(v: f64) -> f64 }

@safe("sqrt of a sum of squares is always in libm's domain")
fn squareRoot(v: f64) -> f64 { sqrt(v) }

fn main() -> i64 {
    io::println(Point { x: 3.0, y: 4.0 }.magnitude())
    io::println(Point { x: 1.0, y: 1.0 }.scaled(3.0).x)
    io::println(7.squared())
    io::println(8.isEven())
    io::println("hey".shout())
    0
}""", mode="run", title="Extending your types and the builtins"),
        P("A generic type is extended the same way. Written without "
          "arguments, `extend` uses the names the type declares; written with "
          "them, it names them itself. Either way the methods belong to the "
          "type, so every instantiation has them — and two blocks may both "
          "add to one type."),
        S('''import std::io

struct Pair<A, B> { first: A, second: B }

extend Pair {
    /// `A` and `B` are the type's own parameters.
    fn swapped(&self) -> Pair<B, A> {
        Pair<B, A> { first: self.second.$clone(), second: self.first.$clone() }
    }
}

extend<X, Y> Pair<X, Y> {
    /// The same thing, with names of this block's choosing.
    fn describe(&self) -> String where X: io::Display, Y: io::Display {
        self.first.display() + "|" + self.second.display()
    }
}

fn main() -> i64 {
    let p = Pair<i64, String> { first: 3, second: "three" }
    io::println(p.describe())
    let q = p.swapped()
    io::println(q.first + " " + q.second.$str())
    0
}''', mode="run", title="Extending a generic type"),
        N("The parameters line up one for one: `extend<A, B> Pair<A, B>`. "
          "Naming a shape instead — `extend<A> Pair<A, i64>` — would be a "
          "partial specialisation, methods on some instantiations and not "
          "others, which this language does not have.",
          label="All of them, in order", tone="warn"),

        H("Mark objects: `dyn Mark`"),
        P("A generic parameter with a mark bound is resolved at compile time. "
          "When the concrete type is only known at run time — a collection of "
          "several unrelated types — use `dyn Mark`, which pairs the value with "
          "a dispatch table."),
        S("""import std::io

mark Shape {
    fn area(&self) -> f64
    fn name(&self) -> String
    fn summary(&self) -> String { self.name() + " " + self.area().$str() }
}

struct Circle { r: f64 }
enum Tile { Small, Large }
class Canvas { pub side: f64
    fn init(self, side: f64) { self.side = side } }

bind Shape to Circle {
    fn area(&self) -> f64 { 3.14159265 * self.r * self.r }
    fn name(&self) -> String { "circle" }
}
bind Shape to Tile {
    fn area(&self) -> f64 { match self { Tile::Small => 1.0, Tile::Large => 4.0 } }
    fn name(&self) -> String { "tile" }
}
bind Shape to Canvas {
    fn area(&self) -> f64 { self.side * self.side }
    fn name(&self) -> String { "canvas" }
}

// Runtime dispatch: any Shape at all.
fn describe(s: dyn Shape) -> String { s.summary() }

// Compile-time dispatch: monomorphised per type, and inlinable.
fn describeStatic<T: Shape>(s: T) -> String { s.summary() }

fn total(shapes: [dyn Shape]) -> f64 {
    var sum = 0.0
    for s in shapes { sum += s.area() }
    sum
}

fn main() -> i64 {
    io::println(describe(Circle { r: 1.0 }))
    io::println(describe(Tile::Large))
    io::println(describe(Canvas(3.0)))
    io::println(describeStatic(Circle { r: 1.0 }))

    let mixed: [3:dyn Shape] = [Circle { r: 2.0 }, Tile::Small, Canvas(2.0)]
    io::println(total(mixed))
    0
}""", mode="run", title="Structs, enums and classes in one collection"),
        T(["", "`dyn Mark`", "`<T: Mark>`"],
          [["Dispatch", "through a table, at run time", "resolved at compile time"],
           ["Code size", "one copy", "one copy per type used"],
           ["Inlining", "no", "yes"],
           ["Mixed collections", "yes", "no"],
           ["Representation", "value plus table, two words",
            "the value itself"]],
          caption="Choosing between them"),
        S("""mark Show { fn show(&self) -> String }
struct Point { x: i64 }

fn render(s: dyn Show) -> String { s.show() }

fn main() -> i64 {
    render(Point { x: 1 })
    0
}""", mode="diag", title="Boxing a type that is not bound"),

        H("Opaque results: `some Mark`"),
        P("A generic parameter with a mark bound is chosen by the *caller*. "
          "`dyn Mark` is chosen at run time and boxed. `some Mark` is the "
          "third spelling: the *body* decides the concrete type, every call "
          "yields that same type, and callers see only the mark. It costs "
          "nothing at run time — there is no box and no table, the value is "
          "the concrete one — which is what makes it the right spelling for "
          "an iterator chain whose nested wrapper type nobody wants to write "
          "out."),
        S("""import std::io
import std::iter

mark Shape { fn area(&self) -> f64 }
struct Sq { pub s: f64 }
struct Circle { pub r: f64 }
bind Shape to Sq { fn area(&self) -> f64 { self.s * self.s } }
bind Shape to Circle { fn area(&self) -> f64 { 3.0 * self.r * self.r } }

fn make(side: f64) -> some Shape { Sq { s: side } }

/// The point of it: a chain whose type nobody has to write out.
fn evens(limit: i64) -> some iter::Iterator {
    iter::counting(0).filter(||(n: i64) -> bool { n % 2 == 0 }).take(limit)
}

fn total<S: Shape>(a: S, b: S) -> f64 { a.area() + b.area() }

struct Box { pub w: f64 }
extend Box {
    pub fn shape(&self) -> some Shape { Sq { s: self.w } }
}

fn main() -> i64 {
    let s = make(3.0)
    io::println(s.area())
    io::println(total(make(1.0), make(2.0)))   // one type per function
    for n in evens(3) { io::println(n) }
    let d: dyn Shape = make(4.0)               // still boxes when asked
    io::println(d.area())
    io::println(Box { w: 2.0 }.shape().area())
    0
}""", mode="run", title="The body picks the type; callers see the mark"),
        P("What is behind a `some` is the function's own business. A caller "
          "cannot name a field of it, treat it as the concrete type, or take "
          "it apart — only the mark's methods, and whatever that mark is "
          "itself bound to."),
        S("""mark Shape { fn area(&self) -> f64 }
struct Sq { pub s: f64 }
bind Shape to Sq { fn area(&self) -> f64 { self.s * self.s } }

fn make(side: f64) -> some Shape { Sq { s: side } }

fn main() -> i64 {
    let s = make(3.0)
    let side = s.s
    0
}""", mode="diag", title="The hidden type is not the caller's to name"),
        P("A `some` stands for exactly one type, fixed by the first value the "
          "body returns. A second, different one is an error — unlike "
          "`dyn Mark`, which is how a function returns one of several."),
        S("""mark Shape { fn area(&self) -> f64 }
struct Sq { pub s: f64 }
struct Circle { pub r: f64 }
bind Shape to Sq { fn area(&self) -> f64 { self.s * self.s } }
bind Shape to Circle { fn area(&self) -> f64 { 3.0 * self.r * self.r } }

fn either(c: bool) -> some Shape {
    if c { return Sq { s: 1.0 } }
    Circle { r: 1.0 }
}

fn main() -> i64 { 0 }""", mode="diag", title="One function, one hidden type"),
        T(["", "`dyn Mark`", "`some Mark`", "`<T: Mark>`"],
          [["Who picks the type", "the caller, at run time",
            "the body, once", "the caller, at compile time"],
           ["Callers see", "the mark", "the mark", "the type parameter"],
           ["Representation", "value plus table, two words",
            "the value itself", "the value itself"],
           ["Mixed collections", "yes", "no — one type per function", "no"],
           ["Cost", "a box, unless it is a class", "none", "none"]],
          caption="Three ways to talk about a mark"),
        N("`some` is not a keyword. It is recognised only where a type is "
          "expected and a mark name follows, so `option::some` and a local "
          "called `some` keep meaning what they did.",
          label="The word is not reserved"),

        H("A requirement with a `where` clause"),
        P("A requirement may carry type parameters and a clause of its own, "
          "and so may a mark's default. The clause is enforced wherever the "
          "method is called — through the type, and through the mark, where "
          "only the requirement is in view."),
        S("""import std::io

mark Show { fn show(&self) -> String }
bind Show to i64 { fn show(&self) -> String { self.$str() } }

mark Sink {
    /// A requirement with a parameter and a bound of its own.
    fn accept<T>(&var self, value: T) -> String where T: Show

    /// A default may carry one too, and call the requirement under it.
    fn acceptTwice<T>(&var self, value: T) -> String where T: Show {
        self.accept(value) + self.accept(value)
    }
}

/// A clause may be about the mark's own associated type rather than a
/// parameter.
mark Holder {
    type Item
    fn render(&self) -> String where Self::Item: Show
}

struct Log { var lines: i64 }
bind Sink to Log {
    fn accept<T>(&var self, value: T) -> String where T: Show {
        self.lines += 1
        value.show()
    }
}

struct Box { v: i64 }
bind Holder to Box {
    type Item = i64
    fn render(&self) -> String where Self::Item: Show { self.v.show() }
}

/// Called through the mark, so the requirement's clause is what is checked.
fn record<S: Sink>(s: &var S, value: i64) -> String { s.accept(value) }

fn main() -> i64 {
    var l = Log { lines: 0 }
    io::println(l.acceptTwice(7))
    io::println(record(&var l, 9))
    io::println(Box { v: 5 }.render())
    io::println(l.lines)
    0
}""", mode="run", title="Clauses on a requirement and on a default"),
        S("""import std::io

mark Show { fn show(&self) -> String }
mark Sink { fn accept<T>(&var self, value: T) -> String where T: Show }

struct Opaque { n: i64 }
struct Log { }
bind Sink to Log {
    fn accept<T>(&var self, value: T) -> String where T: Show { value.show() }
}

fn main() -> i64 {
    var l = Log { }
    io::println(l.accept(Opaque { n: 1 }))
    0
}""", mode="diag", title="The clause is what refuses this"),
        N("An implementation may ask for *more* than the mark promised — a "
          "bind whose `accept` says `where T: Show, T: Total` compiles — but "
          "the extra bound is then enforced at every call, including the ones "
          "that only knew about the mark.", label="Asking for more"),
    ]))

# ===========================================================================
# 14b. Any
# ===========================================================================
SECTIONS.append(Sec(
    "any", "abstraction", "`Any`",
    "`Any` holds one value of any type and remembers which. Every question "
    "about it is answered from the value itself, so the answer cannot be "
    "wrong.",
    keywords=["Any", "dynamic", "typeName", "holds", "get", "expect", "is",
              "boxing", "runtime type", "reflection", "downcast"],
    items=[
        H("Putting a value in"),
        P("Anything with a run-time representation converts to `Any`, and the "
          "conversion is implicit — there is nothing to write."),
        S("""import std::io

struct Point { pub x: f64, pub y: f64 }

fn main() -> i64 {
    let values: [5:Any] = [
        7i64,
        1.5,
        "a string",
        Point { x: 3.0, y: 4.0 },
        (1i64, true),
    ]
    for v in values { io::println(v.typeName()) }
    0
}""", mode="run", title="Five unrelated types in one array"),
        N("`typeName` gives the *qualified* name, so two types called `Point` "
          "in two modules never read as one. Every sample on this page is "
          "compiled as its own module, which is where the `any_2::` prefix "
          "comes from; in your program it is your package's name."),

        H("Getting it back out"),
        T(["Written", "Means"],
          [["`value.typeName()`", "the fully qualified name of the type inside"],
           ["`value is T`", "true when the value inside is a `T`"],
           ["`value.holds::<T>()`", "the same test, for a `T` with no bare-name spelling"],
           ["`value.get::<T>()`", "`T?` — the value, or `nil` when it is something else"],
           ["`value.expect::<T>()`", "`T` — the value, aborting when it is something else"]],
          caption="Everything an `Any` will tell you"),
        P("`get` is the one to reach for. The test and the read happen "
          "together, so there is no way to read the value as a type it does "
          "not have."),
        S("""import std::io

fn main() -> i64 {
    let boxed: Any = 41i64

    io::println(boxed.typeName())
    io::println(boxed is i64)
    io::println(boxed.holds::<f64>())
    io::println(boxed.get::<i64>() ?? -1)
    io::println(boxed.get::<f64>() ?? -1.0)
    io::println(boxed.expect::<i64>() + 1)
    0
}""", mode="run", title="The five questions"),
        N("`is T` and `holds::<T>()` ask the same question. `is` reads better "
          "but only takes a name, because that is where a pattern would have "
          "gone. A type with no bare-name spelling — `[3:f64]`, "
          "`(i64, String)`, `dyn Shape` — goes through `holds` and `get`."),
        S("""import std::io

struct Point { pub x: f64, pub y: f64 }

fn render(v: Any) -> String {
    if v is i64 { return "i64    " + v.expect::<i64>().$str() }
    if v is String { return "String " + v.expect::<String>() }
    if v is Point {
        let p = v.expect::<Point>()
        return "Point  " + p.x.$str() + ", " + p.y.$str()
    }
    match v.get::<(i64, bool)>() {
        Option::Some(pair) => "tuple  " + pair.0.$str() + ", " + pair.1.$str(),
        Option::None => "?      " + v.typeName(),
    }
}

fn main() -> i64 {
    io::println(render(7i64))
    io::println(render("text"))
    io::println(render(Point { x: 1.0, y: 2.0 }))
    io::println(render((3i64, true)))
    io::println(render(2.5))
    0
}""", mode="run", title="Asking, then reading"),

        H("What the answer is based on"),
        P("An `Any` is one pointer: the value itself when it is a class, and a "
          "reference-counted box around it otherwise. Either way the object "
          "header in front of it carries a descriptor naming the type, and "
          "every question above is answered from that descriptor — from the "
          "value, never from a promise the program made about it. Each type "
          "gets one descriptor across the whole program, so the comparison is "
          "a pointer comparison."),
        P("A class is matched the way `is` matches classes everywhere: a `Dog` "
          "also holds as an `Animal`. Everything else is matched exactly — a "
          "`u32` does not hold as an `i64`, even though one converts to the "
          "other."),
        S("""import std::io

class Animal {
    pub name: String
    fn init(self, name: String) { self.name = name }
    pub fn speak(&self) -> String { "..." }
}
class Dog : Animal {
    fn init(self, name: String) { super.init(name) }
    pub fn speak(&self) -> String { "woof" }
}

fn main() -> i64 {
    let d: Any = Dog("rex")
    io::println(d is Dog)
    io::println(d is Animal)
    io::println(d.expect::<Animal>().speak())

    let a: Any = Animal("generic")
    io::println(a is Dog)

    let n: Any = 7i64
    io::println(n.holds::<u32>())
    0
}""", mode="run", title="A class keeps its hierarchy; nothing else widens"),

        H("Ownership"),
        P("An `Any` owns what it holds. Boxing retains, and releasing the "
          "`Any` releases the value; `get` and `expect` hand back a copy, "
          "retained. So a value lives exactly as long as the `Any` holding "
          "it, not as long as the expression that built it."),
        S("""import std::io

class Session {
    pub user: String
    fn init(self, user: String) { self.user = user }
    fn deinit(self) { io::println("closed " + self.user) }
}

fn open() {
    let held: Any = Session("ada")
    io::println("holding a " + held.typeName())
    io::println("user is " + held.expect::<Session>().user)
}

fn main() -> i64 {
    open()
    io::println("past it")
    0
}""", mode="run", title="The session closes with the `Any`"),
        N("A `Unique<T>` cannot go into an `Any`: the `Any` would be its "
          "second owner, and a `Unique` has room for one."),

        H("When it aborts"),
        P("`expect` is for a type that is an invariant of the program rather "
          "than something to be checked. It names both types on the way out."),
        S("""import std::io

fn main() -> i64 {
    let boxed: Any = 3.5
    io::println("before")
    io::println(boxed.expect::<i64>())
    0
}""", mode="panic", title="`expect` on the wrong type"),

        H("Printing one"),
        P("Nothing prints an `Any` by default, because nothing can know how. "
          "A program that wants one to print says so."),
        S("""import std::io

struct Point { pub x: f64 }

bind io::Display to Any {
    fn display(&self) -> String {
        if self is i64 { return self.expect::<i64>().$str() }
        if self is f64 { return self.expect::<f64>().$str() }
        if self is String { return self.expect::<String>() }
        "<" + self.typeName() + ">"
    }
}

fn main() -> i64 {
    let row: [4:Any] = [1i64, 2.5, "three", Point { x: 4.0 }]
    for v in row { io::println(v) }
    0
}""", mode="run", title="`bind io::Display to Any`"),

        H("Which of the three to reach for"),
        T(["Situation", "Reach for"],
          [["The set of types is closed",
            "an `enum`, so `match` makes the compiler check you covered it"],
           ["The types differ, the behaviour does not",
            "`dyn Mark`, which dispatches without asking what the type is"],
           ["Neither: a value from outside the type system, or a container "
            "that takes whatever it is given", "`Any`"]],
          caption="`Any` is the last of the three, not the first"),
        T(["", "`Any`", "`dyn Mark`", "`enum`"],
          [["Types it takes", "every one", "those bound to the mark",
            "the ones listed"],
           ["Checked by the compiler", "no", "the mark's methods",
            "exhaustively, in `match`"],
           ["Dispatch", "none — you ask what it is", "through a table",
            "on the tag"],
           ["Representation", "one pointer", "two words", "tag plus payload"],
           ["Cost to build", "an allocation, unless it is a class",
            "an allocation, unless it is a class", "none"]],
          caption="Side by side"),
    ]))

# ===========================================================================
# Reflection
# ===========================================================================
SECTIONS.append(Sec(
    "reflection", "abstraction", "Compile-time reflection",
    "`std::reflect` asks the compiler what it already knows in order to lay a "
    "value out. Every answer is settled while compiling, so a call costs what "
    "its answer costs — usually nothing at all.",
    [
        P("This is reflection in the sense Rust and Swift mean it — "
          "`size_of`, `type_name`, `offset_of!`, `MemoryLayout` — rather than "
          "the sense where a program discovers types it was not compiled "
          "against. There is no descriptor to carry and nothing to look up. "
          "`Any` is the runtime half, and answers a narrower question: what "
          "is *this value*?"),

        H("Asking about a type"),
        S('''import std::reflect

struct Point { x: f64, y: i32 }

fn main() -> i64 {
    println!("{} {}", reflect::typeName<Point>(), reflect::typeName<[3:f64]>())
    println!("size={} align={} stride={}",
             reflect::sizeOf<Point>(), reflect::alignOf<Point>(),
             reflect::strideOf<Point>())

    // Fields, by position: name, type, and where it begins.
    println!("{} fields", reflect::fieldCount<Point>())
    println!("{}: {} at {}", reflect::fieldName<Point>(0),
             reflect::fieldType<Point>(0), offset_of!(Point, x))
    println!("{}: {} at {}", reflect::fieldName<Point>(1),
             reflect::fieldType<Point>(1), offset_of!(Point, y))

    // An identity to compare, derived from the name.
    println!("{}", reflect::typeId<i64>() == reflect::typeId<i64>())
    0
}''', mode="run", title="What the compiler knows about a type"),
        T(["Asked", "Answers"],
          [["`typeName<T>()`", "the fully qualified name, as `Any` reports it"],
           ["`typeId<T>()`", "a number equal for the same type, derived from "
            "the name"],
           ["`kindOf<T>()`", "which shape it is, as a `reflect::Kind`"],
           ["`sizeOf<T>()`", "bytes one value occupies"],
           ["`alignOf<T>()`", "the alignment it requires"],
           ["`strideOf<T>()`", "the distance between array elements"],
           ["`offset_of!(T, f)`", "where field `f` begins, in bytes"],
           ["`fieldCount<T>()`", "struct fields, tuple elements, enum "
            "variants"],
           ["`fieldName<T>(i)`", "the name of part *i*"],
           ["`fieldType<T>(i)`", "the name of its type"],
           ["`conforms<T, M>()`", "whether `T` binds the mark `M`"]]),
        N("`offset_of!` is a macro because a field is a *name*, not a value: "
          "there is nothing to pass. It is written as a rule in `std::reflect` "
          "— `stringify!` carries the name through to an intrinsic that "
          "resolves it. Nothing about it is built in.",
          label="Why one of them is a macro"),

        H("It really is compile time"),
        P("None of this survives to run time. The whole of `main` below folds "
          "to one constant, and a `match` on `kindOf` keeps only the arm its "
          "type reaches:"),
        SH("""fn main() -> i64 {
    reflect::sizeOf<Point>() as i64 + reflect::alignOf<Point>() as i64 +
        offset_of!(Point, y) as i64 + reflect::typeId<Point>() as i64 % 7
}

$ runec -O1 --emit-llvm -o - main.rune
define i64 @main() {
entry:
  ret i64 27
}"""),
        P("That is what makes `conforms` worth having: a generic function can "
          "take a better path for types that offer one without demanding it "
          "of every type, and the path not taken is never emitted."),
        S('''import std::reflect
import std::io

struct Named { n: i64 }
bind io::Display to Named {
    fn display(&self) -> String { "Named#" + self.n.$str() }
}

struct Plain { n: i64 }

/// One body, two instantiations, each keeping only its own branch.
fn render<T>(value: T) -> String {
    if reflect::conforms<T, io::Display>() { return "displayable" }
    reflect::describe(value)
}

fn main() -> i64 {
    println!("{}", render(Named { n: 1 }))
    println!("{}", render(Plain { n: 2 }))
    0
}''', mode="run", title="Specialising on what a type offers"),

        H("Reading a value"),
        P("`describe` renders a value from its layout. It asks nothing of the "
          "type: no mark to bind and no `display` to write. It is the reading "
          "half of the same idea as `mem::hash` and `mem::equals` — the "
          "compiler already knows what a value is made of, so the program may "
          "as well be able to say it."),
        S('''import std::reflect
import std::io

struct Point { x: i64, y: f64 }
struct Wrap { name: String, at: Point, flags: [2:bool] }
enum Shape { Dot, Circle(f64), Rect { w: i64, h: i64 } }

struct Named { n: i64 }
bind io::Display to Named {
    fn display(&self) -> String { "Named#" + self.n.$str() }
}
struct Holder { inner: Named, other: i64 }

fn main() -> i64 {
    println!("{} {} {}", reflect::describe(42), reflect::describe("hi"),
             reflect::describe('z'))
    println!("{}", reflect::describe(Point { x: 1, y: 2.5 }))
    println!("{} {}", reflect::describe((1, "a")), reflect::describe([1, 2, 3]))
    println!("{}", reflect::describe(Wrap { name: "w",
                                            at: Point { x: 0, y: 0.0 },
                                            flags: [true, false] }))

    // Every enum shape, including the ones the language builds for you.
    let found: i64? = 5
    println!("{} {} {} {}",
             reflect::describe(Shape::Dot),
             reflect::describe(Shape::Circle(1.5)),
             reflect::describe(Shape::Rect { w: 3, h: 4 }),
             reflect::describe(found))

    // A type that decided how it prints keeps that decision, nested too.
    println!("{}", reflect::describe(Holder { inner: Named { n: 7 },
                                              other: 1 }))
    0
}''', mode="run", title="A value, rendered from its layout"),
        T(["Shape", "Rendered as"],
          [["struct", "`Point { x: 1, y: 2.5 }`"],
           ["tuple", "`(1, \"a\")`"],
           ["array", "`[1, 2, 3]`"],
           ["enum", "`Dot`, `Circle(1.5)`, `Rect { w: 3, h: 4 }`"],
           ["`String`", "quoted, so an empty one is visible"],
           ["class", "`Node@0x...` — a reference is what it *is*, and "
            "following it could run forever around a cycle"],
           ["binds `io::Display`", "whatever `display` returns"]]),
        N("`describe` is for looking at values: logs, tests, a quick dump. A "
          "type that wants to control how it prints binds `io::Display`, and "
          "`describe` uses it wherever it finds one — including on a field "
          "inside something else.",
          label="It defers to `Display`"),

        H("What is not here"),
        P("There is no way to build a type, call a method by name, or "
          "enumerate what a program contains. Reflection here reads what the "
          "compiler already worked out; it is not a second, dynamic way to "
          "write programs. Where a set of types is closed, an enum says so "
          "and `match` checks you covered it; where they share behaviour, "
          "`dyn Mark` dispatches without asking what they are."),
        T(["Want", "Reach for"],
          [["What is this *value*?", "`Any` — `holds`, `get`, `typeName`"],
           ["What is this *type*?", "`std::reflect`"],
           ["Dispatch without knowing", "`dyn Mark`"],
           ["A closed set of shapes", "an `enum` and `match`"],
           ["Generate code per field", "a macro; see **Macros**"]]),
    ],
    keywords=["reflect", "reflection", "compile-time", "type_name", "typeName",
              "typeId", "size_of", "sizeOf", "alignOf", "strideOf", "offset_of",
              "offsetOf", "fieldCount", "fieldName", "fieldType", "kindOf",
              "conforms", "describe", "MemoryLayout", "introspection",
              "layout", "metaprogramming"]))


# ===========================================================================
# 15. Operator overloading
# ===========================================================================
SECTIONS.append(Sec(
    "operator-overloading", "abstraction", "Operator overloading",
    "`bind operator::name to Type` gives an operator a meaning for your type. "
    "Every operator maps to one method name.",
    keywords=["operator", "overload", "add", "index", "eq", "cmp", "neg",
              "subscript", "comparison", "deref", "dereference", "smart "
              "pointer", "punctuation", "alias"],
    items=[
        H("The mapping"),
        P("A word, a bracket name, or the punctuation itself in quotes — "
          "`operator::index`, `operator::LeftSquareBracket` and "
          "`operator::\"[]\"` are three ways to write one thing. The method "
          "inside is what actually claims the operator, so a single block may "
          "carry more than one."),
        T(["Operator", "Method", "Also written"],
          [["`+` `-` `*` `/` `%`", "`add` `sub` `mul` `div` `rem`",
            "`Plus` `Minus` `Star` `Slash` `Percent`, or `\"+\"` … `\"%\"`"],
           ["`&` `\\|` `^`", "`bitand` `bitor` `bitxor`",
            "`Ampersand` `Pipe` `Caret`, or `\"&\"` `\"^\"`"],
           ["`<<` `>>`", "`shl` `shr`", "`ShiftLeft` `ShiftRight`"],
           ["`==` `!=`", "`eq`", "`EqualEqual` `Equals`, or `\"==\"`"],
           ["`<` `<=` `>` `>=`", "`cmp`", "`LessThan` `Compare`, or `\"<\"`"],
           ["`-a`", "`neg`", "`Negate`"],
           ["`!a`", "`not`", "`Bang`, or `\"!\"`"],
           ["`~a`", "`bitnot`", "`Tilde`, or `\"~\"`"],
           ["`a[i]`", "`index`", "`LeftSquareBracket` `Subscript`, or `\"[]\"`"],
           ["`*a`", "`deref`", "`Asterisk`, or `\"*\"`"],
           ["`*a = v`", "`derefSet`", "`AsteriskEquals`"]],
          caption="Operator to method name"),
        N("`eq` returns a `bool` and also answers `!=`, inverted. `cmp` returns "
          "a negative number, zero or a positive number and answers all four "
          "relational operators.", label="Two comparison protocols"),

        H("Arithmetic"),
        S("""import std::io

struct Vec2 { pub x: f64, pub y: f64 }

bind operator::add to Vec2 {
    fn add(&self, rhs: &Vec2) -> Vec2 {
        Vec2 { x: self.x + rhs.x, y: self.y + rhs.y }
    }
}
bind operator::sub to Vec2 {
    fn sub(&self, rhs: &Vec2) -> Vec2 {
        Vec2 { x: self.x - rhs.x, y: self.y - rhs.y }
    }
}
bind operator::mul to Vec2 {
    fn mul(&self, k: f64) -> Vec2 {
        Vec2 { x: self.x * k, y: self.y * k }
    }
}
bind operator::neg to Vec2 {
    fn neg(&self) -> Vec2 { Vec2 { x: 0.0 - self.x, y: 0.0 - self.y } }
}

fn show(v: Vec2) -> String { "(" + v.x.$str() + "," + v.y.$str() + ")" }

fn main() -> i64 {
    let a = Vec2 { x: 1.0, y: 2.0 }
    let b = Vec2 { x: 0.5, y: 0.5 }

    io::println(show(a + b))
    io::println(show(a - b))
    io::println(show(a * 3.0))
    io::println(show(-a))

    // A compound assignment uses the same overload.
    var acc = Vec2 { x: 0.0, y: 0.0 }
    acc += a
    acc += b
    io::println(show(acc))
    0
}""", mode="run", title="`+ - * -a` and `+=`"),

        H("Equality and ordering"),
        S("""import std::io

struct Version { major: i64, minor: i64 }

bind operator::eq to Version {
    fn eq(&self, rhs: &Version) -> bool {
        self.major == rhs.major && self.minor == rhs.minor
    }
}

bind operator::cmp to Version {
    fn cmp(&self, rhs: &Version) -> i64 {
        if self.major != rhs.major { return self.major - rhs.major }
        self.minor - rhs.minor
    }
}

fn main() -> i64 {
    let a = Version { major: 1, minor: 2 }
    let b = Version { major: 1, minor: 9 }

    io::println(a == a)
    io::println(a != b)      // `eq`, inverted
    io::println(a < b)       // all four come from `cmp`
    io::println(a <= b)
    io::println(b > a)
    io::println(b >= b)
    0
}""", mode="run", title="One `eq` and one `cmp` cover six operators"),

        H("Subscripting"),
        S("""import std::io

struct Grid {
    cells: [9:i64]
    width: i64
}

bind operator::index to Grid {
    fn index(&self, at: i64) -> i64 { self.cells[at] }
}

struct Lookup { keys: [3:String], values: [3:i64] }

bind operator::LeftSquareBracket to Lookup {
    fn index(&self, key: String) -> i64 {
        for i in 0..3 {
            if self.keys[i] == key { return self.values[i] }
        }
        -1
    }
}

fn main() -> i64 {
    let g = Grid { cells: [1, 2, 3, 4, 5, 6, 7, 8, 9], width: 3 }
    io::println(g[0].$str() + " " + g[4].$str() + " " + g[8].$str())

    let table = Lookup {
        keys: ["one", "two", "three"],
        values: [1, 2, 3],
    }
    io::println(table["two"].$str() + " " + table["missing"].$str())
    0
}""", mode="run", title="An index of any type you like"),
        P("And more than one, on one type. `index` overloads the way every "
          "other bound method does — by what it takes — so a value can be "
          "reached both by position and by name, and `indexSet` follows the "
          "`index` the read chose."),
        S("""import std::io

struct Row { pub id: i64, pub name: String }

bind operator::"[]" to Row {
    fn index(&self, at: i64) -> String {
        if at == 0 { self.id.$str() } else { self.name }
    }
    fn indexSet(&var self, at: i64, value: String) {
        if at == 0 { self.id = value.$toInt() ?? 0 } else { self.name = value }
    }
}

// A second block for the same operator on the same type. What is between the
// brackets is what says which one runs.
bind operator::"[]" to Row {
    fn index(&self, field: String) -> String {
        if field == "id" { self.id.$str() } else { self.name }
    }
    fn indexSet(&var self, field: String, value: String) {
        if field == "id" { self.id = value.$toInt() ?? 0 } else { self.name = value }
    }
}

fn main() -> i64 {
    var r = Row { id: 7, name: "ada" }
    io::println(r[0])          // by position
    io::println(r["name"])     // by name
    r["id"] = "9"
    r[1] = "grace"
    io::println(r[0] + " " + r[1])
    0
}""", mode="run", title="Indexed two ways"),

        H("Dereference: making a value behave like a pointer"),
        P("`deref` says what `*value` produces; `derefSet` says what `*value = "
          "x` does. A type with both behaves like a pointer, including through "
          "compound assignment — `*h += 5` reads once, applies the operator, "
          "and writes once."),
        S("""import std::io

struct Celsius { deg: f64 }

// The punctuation spelling; the two methods claim their own operators.
bind operator::"*" to Celsius {
    fn deref(&self) -> f64 { self.deg }
    fn derefSet(&var self, value: f64) { (*self).deg = value }
}

fn main() -> i64 {
    var c = Celsius { deg: 21.5 }
    io::println((*c).$str())
    *c = 30.0
    io::println((*c).$str())
    *c += 2.5
    io::println((*c).$str())
    0
}""", mode="run", title="`*` on a value of your own"),
        P("`std::mem` binds both for `Handle<T>`, which is what makes a handle "
          "read like a pointer to the value it owns."),
        S("""import std::io
import std::mem

fn main() -> i64 {
    var h = mem::Handle<i64>(41)
    *h = *h + 1
    io::println((*h).$str())

    var s = mem::of("hello")
    *s += " there"
    io::println(*s)
    0
}""", mode="run", title="A handle is a pointer"),
        P("`@alias` on the block registers a further spelling for the same "
          "operator, everywhere. That is the one case where an alias is not "
          "local to the declaration it decorates."),
        S("""import std::io

struct Celsius { deg: f64 }

@alias("degrees")
bind operator::"*" to Celsius {
    fn deref(&self) -> f64 { self.deg }
}

struct Kelvin { deg: f64 }

// `degrees` now names the same operator as `*` does.
bind operator::"degrees" to Kelvin {
    fn deref(&self) -> f64 { self.deg - 273.15 }
}

fn main() -> i64 {
    io::println((*Celsius { deg: 21.5 }).$str())
    io::println((*Kelvin { deg: 373.15 }).$str())
    0
}""", mode="run", title="An operator spelling of your own"),
        S("""struct Reading { v: f64 }

bind operator::"*" to Reading {
    fn deref(&self) -> f64 { self.v }
}

fn main() -> i64 {
    var r = Reading { v: 1.0 }
    *r = 2.0
    0
}""", mode="diag", title="Readable through `*`, but not writable"),

        H("One operator, several right-hand types"),
        P("An operator is overloaded **once per right-hand type**. A type may "
          "therefore define `+` against several others, and which one runs is "
          "decided by what is on the right — including for compound "
          "assignment, where the two sides need not be the same type at all."),
        S('import std::io\n\nstruct Money { pub cents: i64 }\nstruct Rate  { pub percent: i64 }\n\n// Three overloads of `+`, told apart by what is on the right.\nbind operator::add to Money {\n    fn add(&self, rhs: &Money) -> Money { Money { cents: self.cents + rhs.cents } }\n}\nbind operator::add to Money {\n    fn add(&self, rhs: i64) -> Money { Money { cents: self.cents + rhs } }\n}\nbind operator::add to Money {\n    fn add(&self, rhs: &Rate) -> Money {\n        Money { cents: self.cents + self.cents * rhs.percent / 100 }\n    }\n}\n\nfn main() -> i64 {\n    let m = Money { cents: 100 }\n    io::println((m + Money { cents: 50 }).cents.$str())\n    io::println((m + 7).cents.$str())\n    io::println((m + Rate { percent: 10 }).cents.$str())\n\n    // Compound assignment picks the same way, so the right side need not\n    // be the type on the left.\n    var running = Money { cents: 100 }\n    running += Rate { percent: 50 }\n    io::println(running.cents.$str())\n    0\n}', mode="run", title="`+` three ways on one type"),
        N("An exact match on the right-hand type wins. Failing that, the first "
          "overload whose parameter would accept the value is used, so an "
          "overload taking `i64` also serves an `i32` on the right.",
          label="How one is chosen"),
        P("This is not special to arithmetic. Every method a `bind` supplies "
          "may be written more than once and told apart by what it takes — "
          "`index` and `indexSet` above, and any mark requirement too. See "
          "[One name, several parameter lists](#marks)."),

        H("Extending a builtin, without overriding it"),
        P("A builtin's own meanings are the language's and stay that way — but "
          "the pairs it has *no* meaning for are yours to give. `String + "
          "String` is built in and cannot be replaced; `String + Character` is "
          "not, so the standard library defines it:"),
        S('import std::io\n\nfn main() -> i64 {\n    var text = ""\n    text += \'a\'          // String + Character, from std::io\n    text += \'b\'\n    io::println(text)\n    0\n}', mode="run", title="Appending a character"),
        P("The rule is one sentence: **an overload may teach a type to work "
          "with another type, but never replace what the language already "
          "does.** Anything that would shadow a builtin meaning is refused "
          "rather than silently ignored."),
        S("""bind operator::add to String {
    fn add(&self, rhs: String) -> String { self }
}

fn main() -> i64 { 0 }""", mode="diag", title="Redefining `String + String`"),

        H("What cannot be overloaded"),
        P("`&&`, `||` and `??` cannot be overloaded: they short-circuit, so "
          "they never evaluate their right side unconditionally, and a method "
          "call would have to."),
        H("Automatic marks"),
        P("Some marks are not promises a type makes but facts about it: it "
          "holds nothing that has to be destroyed, everything in it can be "
          "copied, nothing in it stops it crossing to another thread. "
          "`@auto` says so, and the compiler answers for every type: a type "
          "has an automatic mark when **every part of it** has it."),
        S('''import std::io

/// A claim about a type, not a promise it makes: it holds plain values.
@auto
mark Plain {}

struct Point { x: i64, y: i64 }        // has it: two integers
struct Pair { first: Point, at: (i64, bool) }   // has it: so do its parts

fn describe<T: Plain>(value: &T) -> String { "plain" }

fn main() -> i64 {
    let p = Point { x: 1, y: 2 }
    io::println(describe(&p))
    0
}''', mode="run", title="A mark the compiler answers for"),
        P("An automatic mark carries no requirements — there is nobody to "
          "implement them, since nobody writes the binding. What it carries "
          "is the rule, and two ways to override it where the structure has "
          "nothing to say."),
        T(["Written", "Means"],
          [["`@auto mark M {}`", "M is automatic: every part decides"],
           ["`bind M to T {}`", "T has it, whatever its parts say"],
           ["`@never(M)` on `T`", "T does not have it, whatever its parts say"],
           ["`reflect::conforms<T, M>()`", "the answer, at compile time"]]),
        P("Three things never have one on their own. A type that runs a "
          "`deinit` — a destructor is a promise the compiler cannot read. "
          "Anything it cannot look into: a closure and its captures, an "
          "`Any`, a `dyn Mark`, a raw or `weak` pointer. And a type that "
          "refuses it with `@never`, along with everything holding one."),
        S('''@auto
mark Plain {}

struct Descriptor { fd: i32 }
extend Descriptor {
    fn deinit(&var self) { }
}

fn describe<T: Plain>(value: &T) -> String { "plain" }

fn main() -> i64 {
    let d = Descriptor { fd: 3 }
    describe(&d)
    0
}''', mode="diag", title="What a `deinit` costs"),
        N("`bind M to T {}` is the escape hatch, and it is deliberately a "
          "line of code: claiming that a type has a mark its parts do not is "
          "exactly the kind of thing that should be written down. It is how "
          "`String` comes by `mem::Clone` — a string's contents never change, "
          "so a copy of one is a copy.", label="Claiming one"),
        P("`std::mem::Clone` is the automatic mark the standard library "
          "ships. `std::thread::Send` and `std::thread::Sync` answer the same "
          "way, with rules of their own about references and shared mutable "
          "objects — see *Threads and sharing*."),
    ]))

# ===========================================================================
# 16. Generics
# ===========================================================================
SECTIONS.append(Sec(
    "generics", "abstraction", "Generics",
    "Type parameters are monomorphised: each set of arguments produces its own "
    "specialised copy, so there is no boxing and no dynamic dispatch.",
    keywords=["generic", "type parameter", "bound", "where", "turbofish",
              "monomorphisation", "inference", "template", "specialisation", "specialization",
              "shape", "structural bind", "slice bind", "narrower"],
    items=[
        H("Generic functions"),
        P("Type arguments are usually inferred from the call. When they cannot "
          "be, write them with a turbofish."),
        S("""import std::io

fn identity<T>(value: T) -> T { value }

fn pair<A, B>(a: A, b: B) -> (A, B) { (a, b) }

fn firstOr<T>(values: [T], fallback: T) -> T {
    if values.$isEmpty() { return fallback }
    values[0]
}

fn main() -> i64 {
    io::println(identity(7))
    io::println(identity("text"))
    io::println(identity::<f64>(2.5))       // written out

    let p = pair(1, "one")
    io::println(p.0.$str() + " " + p.1)

    let numbers: [3:i64] = [4, 5, 6]
    io::println(firstOr(numbers, -1))
    io::println(firstOr([0; 0], -1))
    0
}""", mode="run", title="Inference, and the turbofish when it is needed"),

        H("Generic methods"),
        P("A method may have type parameters of its own. They are inferred "
          "from the arguments where there is something to infer from, and "
          "written out where there is not."),
        S("""import std::io
import std::mem

class Box {
    pub tag: i64
    fn init(self, tag: i64) { self.tag = tag }

    pub fn pick<T>(&self, v: T) -> T { v }
    pub fn width<T>(&self) -> i64 { mem::size_of<T>() as i64 }
}

fn main() -> i64 {
    let b = Box(1)
    io::println(b.pick(3))              // T inferred from the argument
    io::println(b.pick("text"))
    io::println(b.width::<f64>())       // nothing to infer from
    io::println(b.width::<i32>())
    0
}""", mode="run", title="Type parameters on a method"),
        N("A generic method is dispatched from the type at the call site, "
          "never through a vtable: each set of arguments is a different "
          "function, so there is no single address a slot could hold."),
        S("""import std::mem

class Box {
    pub tag: i64
    fn init(self, tag: i64) { self.tag = tag }
    pub fn width<T>(&self) -> i64 { mem::size_of<T>() as i64 }
}

fn main() -> i64 {
    Box(1).width()
}""", mode="diag", title="Nothing to infer from"),

        H("Bounds"),
        P("A bound says what the parameter must be able to do. Without one, the "
          "body can only move the value around. `<T: Mark>` and a `where` "
          "clause say the same thing and are checked the same way; `where` "
          "exists for the bound whose subject is not a parameter name."),
        S("""import std::io
import std::iter

mark Weighed {
    fn weight(&self) -> i64
}

bind Weighed to i64 { fn weight(&self) -> i64 { self } }
bind Weighed to String { fn weight(&self) -> i64 { self.$length() } }

fn heaviest<T: Weighed>(a: T, b: T) -> T {
    if a.weight() >= b.weight() { a } else { b }
}

// Several bounds with `+`, or spelled out in a `where` clause.
fn describe<T: Weighed + io::Display>(value: T) -> String {
    value.display() + " weighs " + value.weight().$str()
}

fn compare<A, B>(a: A, b: B) -> i64
where A: Weighed, B: Weighed
{
    a.weight() - b.weight()
}

// What `where` adds is a subject that is not a parameter name. There is
// nowhere else to say this: `Iter::Item` is reached *through* a parameter.
fn firstWeight<S>(seq: S) -> i64
where S: iter::Sequence, S::Iter::Item: Weighed
{
    for item in seq { return item.weight() }
    0
}

fn main() -> i64 {
    io::println(heaviest(3, 9))
    io::println(heaviest("ab", "abcd"))
    io::println(describe(42))
    io::println(describe("hello"))
    io::println(compare("abc", 1))
    0
}""", mode="run", title="`T: Mark`, `T: A + B`, and `where`"),
        S("""mark Weighed { fn weight(&self) -> i64 }

struct Feather { grams: f64 }

fn heaviest<T: Weighed>(a: T, b: T) -> T { a }

fn main() -> i64 {
    heaviest(Feather { grams: 0.1 }, Feather { grams: 0.2 })
    0
}""", mode="diag", title="An unmet bound points at both ends"),

        P("A bound may also name an *operator* rather than a mark. "
          "`T: operator::cmp` says the argument has to compare, whoever it "
          "got that from — which is often more direct than inventing a mark "
          "for it."),
        P("The question is whether the operator *works* on `T`, not whether "
          "somebody wrote it out. A builtin type therefore satisfies such a "
          "bound with no `bind` at all: `3 < 9` needs nobody's permission, "
          "and a generic that compares should accept `i64` for the same "
          "reason ordinary code does."),
        S("""import std::io

struct Money { cents: i64 }

bind operator::cmp to Money {
    fn cmp(&self, other: Money) -> i64 { self.cents - other.cents }
}
bind operator::add to Money {
    fn add(&self, other: Money) -> Money {
        Money { cents: self.cents + other.cents }
    }
}

fn largest<T: operator::cmp>(a: T, b: T) -> T { if a > b { a } else { b } }
fn total<T>(a: T, b: T) -> T where T: operator::add { a + b }

fn main() -> i64 {
    // A type that says how it compares.
    let a = Money { cents: 250 }
    let b = Money { cents: 195 }
    io::println(largest(a, b).cents.$str())
    io::println(total(a, b).cents.$str())

    // And the ones the language already knows how to compare.
    io::println(largest(3, 9).$str())
    io::println(largest("ab", "cd"))
    io::println(total(2.5, 1.5).$str())
    0
}""", mode="run", title="Bounded by an operator"),
        S("""struct Plain { v: i64 }
fn largest<T: operator::cmp>(a: T, b: T) -> T { if a > b { a } else { b } }
fn main() -> i64 {
    largest(Plain { v: 1 }, Plain { v: 2 }).v
}""", mode="diag", title="A type that does not overload it"),
        N("Any operator name works, in any spelling — `operator::cmp`, "
          "`operator::LessThan`, `operator::\"<\"` — and several may be "
          "combined with `+` just like mark bounds.", label="Which names"),

        H("Generic types"),
        S("""import std::io

struct Pair<A, B> {
    pub first: A
    pub second: B

    pub fn swapped(&self) -> Pair<B, A> {
        Pair<B, A> { first: self.second, second: self.first }
    }
}

struct Stack<T> {
    items: [8:T]
    count: i64

    pub fn depth(&self) -> i64 { self.count }
    pub fn top(&self, empty: T) -> T {
        if self.count == 0 { return empty }
        self.items[self.count - 1]
    }
}

fn main() -> i64 {
    let p = Pair<i64, String> { first: 1, second: "one" }
    io::println(p.first.$str() + " " + p.second)

    let flipped = p.swapped()
    io::println(flipped.first + " " + flipped.second.$str())

    let s = Stack<i64> { items: [10, 20, 30, 0, 0, 0, 0, 0], count: 3 }
    io::println(s.depth().$str() + " " + s.top(-1).$str())
    0
}""", mode="run", title="Generic structs, and a method that changes the parameters"),

        H("Generic methods"),
        P("A method may introduce its own type parameters, independent of the "
          "type's."),
        S("""import std::io

struct Holder<T> {
    pub value: T

    pub fn mapped<U>(&self, f: @function(T) -> U) -> Holder<U> {
        Holder<U> { value: f(self.value) }
    }
}

fn main() -> i64 {
    let n = Holder<i64> { value: 21 }
    let doubled = n.mapped::<i64>(||(v: i64) -> i64 { v * 2 })
    let described = n.mapped::<String>(||(v: i64) -> String { "n=" + v.$str() })

    io::println(doubled.value)
    io::println(described.value)
    0
}""", mode="run", title="A method with its own parameter"),

        H("How instantiation is reported"),
        P("A generic body is checked once per set of arguments. When something "
          "inside it fails, the diagnostic carries both the failure and the "
          "call that caused it — the same shape as an unmet bound."),
        S("""fn lengthOf<T>(value: T) -> i64 {
    value.$length()
}

fn main() -> i64 {
    lengthOf("text")
    lengthOf(42)
    0
}""", mode="diag", title="The call site travels with the error"),
        H("Specialisation"),
        P("More than one `bind` may apply to one type. The narrower one wins, "
          "and which is narrower does not depend on the order they were "
          "written in."),
        T(["Narrower", "Than"],
          [["a target written out — `Wrapper<i64>`",
            "one with parameters — `Wrapper<T>`"],
           ["more of the shape pinned down — `Boxed<Boxed<T>>`",
            "less — `Boxed<T>`"],
           ["more asked of the parameters — `where T: Tag`",
            "less — no clause at all"]],
          caption="What makes one binding narrower than another, in order"),
        S("""import std::io

struct Wrapper<T> { value: T }
struct Boxed<T> { inner: T }
mark Show { fn show(&self) -> String }
mark Tag  { fn tag(&self) -> String }
bind Tag to i64 { fn tag(&self) -> String { "t" } }

// Written *after* the general case, and still the one that applies.
bind<T> Show to Wrapper<T> { fn show(&self) -> String { "some wrapper" } }
bind Show to Wrapper<i64>  { fn show(&self) -> String { "a wrapped i64" } }

mark Kind { fn kind(&self) -> String }
bind<T> Kind to Wrapper<T> { fn kind(&self) -> String { "any wrapper" } }
bind<T> Kind to Wrapper<T> where T: Tag { fn kind(&self) -> String { "a tagged one" } }

mark Depth { fn depth(&self) -> String }
bind<T> Depth to Boxed<T> { fn depth(&self) -> String { "plain" } }
bind<T> Depth to Boxed<Boxed<T>> { fn depth(&self) -> String { "nested" } }

fn main() -> i64 {
    io::println((Wrapper<i64> { value: 1 }).show())
    io::println((Wrapper<f64> { value: 1.0 }).show())
    io::println((Wrapper<i64> { value: 1 }).kind())
    io::println((Wrapper<f64> { value: 1.0 }).kind())
    io::println((Boxed<i64> { inner: 1 }).depth())
    io::println((Boxed<Boxed<i64>> { inner: Boxed<i64> { inner: 1 } }).depth())
    0
}""", mode="run", title="Three ways to be narrower"),
        P("Two bindings of one mark that are equally specific are reported "
          "rather than decided by the order they were reached in."),
        S("""import std::io

struct Wrapper<T> { value: T }
mark Show { fn show(&self) -> String }
mark Tag  { fn tag(&self) -> String }
mark Mood { fn mood(&self) -> String }
bind Tag to i64  { fn tag(&self) -> String { "t" } }
bind Mood to i64 { fn mood(&self) -> String { "m" } }

bind<T> Show to Wrapper<T> where T: Tag  { fn show(&self) -> String { "tagged" } }
bind<T> Show to Wrapper<T> where T: Mood { fn show(&self) -> String { "other" } }

fn main() -> i64 {
    io::println((Wrapper<i64> { value: 1 }).show())
    0
}""", mode="diag", title="Neither one is narrower"),
        N("Clauses that cannot both hold are not a clash — one type takes the "
          "first and another the second. Neither are two *different* marks "
          "that happen to name a method the same way: `Iterator` and "
          "`Sequence` both supply `map`, and which one answers is settled by "
          "the mark asked for.", label="What is not a clash"),

        H("Binding a shape"),
        P("A `bind` target may be a shape rather than a declaration — `[T]`, "
          "`[N:T]`, `(A, B)`, `&T` — and then every type of that shape gets "
          "the binding. An array is usable wherever a slice is, so one "
          "written for `[T]` covers both."),
        S("""import std::io

/// A mark of one's own, over every slice and array of a summable element.
mark Total { fn total(&self) -> i64 }
bind Total to i64 { fn total(&self) -> i64 { self } }

bind<T> Total to [T] where T: Total {
    fn total(&self) -> i64 {
        var sum = 0
        for i in 0..self.$length() { sum += self[i].total() }
        sum
    }
}

bind<A, B> Total to (A, B) where A: Total, B: Total {
    fn total(&self) -> i64 { self.0.total() + self.1.total() }
}

fn sum<T: Total>(value: T) -> i64 { value.total() }

fn main() -> i64 {
    let ints: [3:i64] = [1, 2, 3]
    io::println(sum(ints))          // an array
    io::println(sum(ints[1..3]))    // a slice of it
    io::println(sum((4, 5)))        // a pair
    0
}""", mode="run", title="One binding, every slice and array"),
        P("`std::io` uses exactly this, which is why an array prints without "
          "anything having to be written for its element type."),
        S("""import std::io

fn main() -> i64 {
    io::println([1, 2, 3])
    io::println((7, "seven"))
    io::println([[1, 2], [3, 4]])
    println!("{} and {}", [1.5, 2.5], (1, true))
    0
}""", mode="run", title="What the library binds for you"),
        N("The `where` clause is what decides which shapes are covered. A "
          "slice of something that is not `Display` is not `Display` either, "
          "and asking is how you find out.", label="Only where it holds"),
    ]))

# ===========================================================================
# 17. Option and Result
# ===========================================================================
SECTIONS.append(Sec(
    "option-result", "standard types", "Option and Result",
    "Both are ordinary Rune enums that the compiler knows by name. `T?`, `nil`, "
    "`??` and `?` are sugar over them, and their variants are in scope "
    "everywhere.",
    keywords=["Option", "Result", "Some", "None", "Ok", "Err", "nil",
              "coalesce", "try", "question mark", "unwrap", "optional",
              "error conversion", "As", "mapErr"],
    items=[
        H("What they are"),
        P("There is no compiler magic in the types themselves — they are "
          "declared in the standard library and you could have written them:"),
        S("""pub enum Option<T> {
    None,
    Some(T),
}

pub enum Result<T, E> {
    Ok(T),
    Err(E),
}""", mode="frag", title="The declarations, from std::option and std::result"),
        T(["You write", "Which means"],
          [["`i64?`", "`Option<i64>`"],
           ["`nil`", "`Option::None`, with `T` taken from context"],
           ["`a ?? b`", "the value in `a`, or `b` when it is `None`"],
           ["`value?`", "return early on `None` or `Err`, else the payload"],
           ["`Some(v)` `None`", "the variants, in scope without an import"],
           ["`Ok(v)` `Err(e)`", "likewise"]],
          caption="The sugar and what it lowers to"),

        H("Producing an Option"),
        S("""import std::io

struct Config { retries: i64, host: String }

fn parseRetries(text: String) -> i64? { text.$toInt() }

// `?` gives up early, so the failure is answered for once, at the end.
fn load(host: String, retries: String) -> Config? {
    let n = parseRetries(retries)?
    if n < 0 { return nil }
    Config { retries: n, host: host }
}

fn describe(c: Config?) -> String {
    match c {
        Some(cfg) => cfg.host + " x" + cfg.retries.$str(),
        None => "unusable",
    }
}

fn main() -> i64 {
    io::println(describe(load("example.com", "3")))
    io::println(describe(load("example.com", "-1")))
    io::println(describe(load("example.com", "many")))
    0
}""", mode="run", title="`?` gives up early, once"),
        S("""import std::io
import std::option

fn firstEven(values: [6:i64]) -> i64? {
    for v in values {
        if v % 2 == 0 { return v }      // wrapped as Some(v)
    }
    nil                                  // Option::None
}

fn explicit(flag: bool) -> i64? {
    if flag { Some(9) } else { None }    // spelled out
}

fn main() -> i64 {
    let evens: [6:i64] = [1, 3, 8, 5, 7, 9]
    let odds: [6:i64] = [1, 3, 5, 7, 9, 11]

    io::println(firstEven(evens).or(-1))
    io::println(firstEven(odds).or(-1))
    io::println(explicit(true).or(-1))
    io::println(explicit(false).or(-1))

    // Built through the helper functions, when there is no context to infer.
    io::println(option::some(3).or(0))
    io::println(option::none::<i64>().or(0))
    0
}""", mode="run", title="Returning, wrapping, and building explicitly"),

        H("Consuming an Option"),
        S("""import std::io

fn lookup(key: String) -> i64? {
    if key == "a" { return 1 }
    if key == "b" { return 2 }
    nil
}

fn main() -> i64 {
    // A default, two ways.
    io::println(lookup("a") ?? -1)
    io::println(lookup("z") ?? -1)
    io::println(lookup("z").or(-1))

    // Questions.
    io::println(lookup("a").hasValue())
    io::println(lookup("z").isNil())

    // Taking the value out, when you know it is there.
    io::println(lookup("b").unwrap())

    // Matching, which handles both cases at once.
    match lookup("b") {
        Some(v) => io::println("found " + v.$str()),
        None => io::println("nothing"),
    }

    // Or the two-case form.
    if lookup("a") is Some(v) {
        io::println("also found " + v.$str())
    }
    0
}""", mode="run", title="Every way to get at the value"),
        T(["Method", "Result"],
          [["`hasValue()`", "`bool` — true when a value is present"],
           ["`isNil()`", "`bool` — the opposite"],
           ["`or(fallback)`", "the value, or `fallback`"],
           ["`unwrap()`", "the value; aborts when empty"],
           ["`expect(message)`", "the value; aborts with `message` when empty"]],
          caption="Option's methods"),
        S("""import std::io

fn main() -> i64 {
    let empty: i64? = nil
    io::println("about to unwrap")
    io::println(empty.unwrap())
    0
}""", mode="panic", title="`unwrap` on an empty Option aborts"),

        H("Result"),
        S("""import std::io
import std::result

type Parsed = result::Result<i64, String>

fn parse(text: String) -> Parsed {
    match text.$toInt() {
        Some(n) => Ok(n),
        None => Err("'" + text + "' is not a number"),
    }
}

fn main() -> i64 {
    match parse("42") {
        Ok(v) => io::println("ok " + v.$str()),
        Err(e) => io::println("err " + e),
    }
    match parse("forty") {
        Ok(v) => io::println("ok " + v.$str()),
        Err(e) => io::println("err " + e),
    }

    io::println(parse("1").isOk())
    io::println(parse("x").isErr())
    io::println(parse("x").or(-1))
    io::println(parse("7").unwrap())

    // A Result narrows to an Option when the reason stops mattering.
    io::println(parse("5").ok().or(0))
    io::println(parse("x").ok().or(0))

    match parse("x").error() {
        Some(message) => io::println("reason: " + message),
        None => io::println("no error"),
    }
    0
}""", mode="run", title="Constructing, testing and unwrapping"),
        T(["Method", "Result"],
          [["`isOk()` `isErr()`", "`bool`"],
           ["`or(fallback)`", "the value, or `fallback`"],
           ["`unwrap()`", "the value; aborts on an error"],
           ["`expect(message)`", "the value; aborts with `message`"],
           ["`ok()`", "`Option<T>` — the value, discarding the error"],
           ["`error()`", "`Option<E>` — the error, discarding the value"]],
          caption="Result's methods"),

        H("`?`, on both"),
        P("`?` unwraps or returns early. The function it appears in has to "
          "return the same shape: an Option for an Option, a Result for a "
          "Result. The error travels out as the function's own error type — "
          "the same type passes through untouched, and a different one is "
          "converted, as the next heading describes."),
        S("""import std::io
import std::result

fn firstEven(values: [4:i64]) -> i64? {
    for v in values { if v % 2 == 0 { return v } }
    nil
}

fn halfOfFirstEven(values: [4:i64]) -> i64? {
    let found = firstEven(values)?        // returns None from here
    found / 2
}

fn parse(text: String) -> result::Result<i64, String> {
    match text.$toInt() {
        Some(n) => Ok(n),
        None => Err("bad number: " + text),
    }
}

fn sumOf(a: String, b: String, c: String) -> result::Result<i64, String> {
    Ok(parse(a)? + parse(b)? + parse(c)?)  // the first Err travels out
}

fn main() -> i64 {
    io::println(halfOfFirstEven([1, 8, 3, 5]).or(-1))
    io::println(halfOfFirstEven([1, 3, 5, 7]).or(-1))

    match sumOf("1", "2", "3") {
        Ok(v) => io::println("total " + v.$str()),
        Err(e) => io::println(e),
    }
    match sumOf("1", "two", "3") {
        Ok(v) => io::println("total " + v.$str()),
        Err(e) => io::println(e),
    }
    0
}""", mode="run", title="Chaining fallible steps"),
        S("""import std::io

fn firstEven(values: [4:i64]) -> i64? {
    for v in values { if v % 2 == 0 { return v } }
    nil
}

fn wrong(values: [4:i64]) -> i64 {
    firstEven(values)?
}

fn main() -> i64 { 0 }""", mode="diag", title="`?` needs a matching result type"),

        H("`?` converts the error"),
        P("A layered program has layered error types: the file layer fails "
          "with a `FileError`, the parser with a `ParseError`, and the "
          "application with something that wraps both. `?` hands the error "
          "out as the function's own error type, so when the two differ it "
          "looks for a way from one to the other — `bind Theirs into Ours`, "
          "the same as `bind As<Ours> to Theirs` — and calls its `convert` "
          "on the way out. That is the same `As` that `into` dispatches "
          "through, so writing the conversion once serves both. An error type "
          "that converts implicitly, a narrow integer into a wide one, is "
          "simply widened."),
        S("""import std::io

enum ParseError { Empty, NotANumber { text: String } }
enum AppError { Parse(ParseError), Disk(io::FileError) }

bind ParseError into AppError {
    fn convert(&self) -> AppError { AppError::Parse(*self) }
}
bind io::FileError into AppError {
    fn convert(&self) -> AppError { AppError::Disk(*self) }
}

fn parse(text: String) -> Result<i64, ParseError> {
    if text.$isEmpty() { return ParseError::Empty }
    match text.$toInt() {
        Some(n) => n,
        None => ParseError::NotANumber { text: text },
    }
}

fn run(text: String) -> Result<i64, AppError> {
    let n = parse(text)?                            // ParseError becomes AppError
    let contents = io::readToString("/no/such/file")?   // and so does FileError
    n + contents.$length()
}

fn describe(e: AppError) -> String {
    match e {
        AppError::Parse(ParseError::Empty) => "parse: empty",
        AppError::Parse(ParseError::NotANumber { text }) => "parse: not a number: " + text,
        AppError::Disk(f) => "disk: " + io::describe(f),
    }
}

fn main() -> i64 {
    match run("abc") { Ok(v) => io::println(v), Err(e) => io::println(describe(e)) }
    match run("42") { Ok(v) => io::println(v), Err(e) => io::println(describe(e)) }
    0
}""", mode="run", title="Layered errors without a `mapErr` at every boundary"),
        S("""enum A { X }
enum B { Y }

fn a() -> Result<i64, A> { A::X }
fn b() -> Result<i64, B> { let v = a()?; v }

fn main() -> i64 { 0 }""", mode="diag", title="Nothing says how to get from A to B"),
        N("The conversion has to be written on the *function's* error type, "
          "which is what makes it safe: a `bind FileError into AppError` "
          "changes what `?` does only inside functions that return "
          "`Result<_, AppError>`, and says so where they are declared.",
          label="Why the destination is the declared result"),

        H("Options of reference types"),
        P("An `Option` of a class or a `String` is an ordinary enum, so it "
          "participates in reference counting like anything else. A `None` holds "
          "nothing to release."),
        S("""import std::io

class Session {
    pub id: i64
    fn init(self, id: i64) { self.id = id }
    fn deinit(self) { io::println("  session " + self.id.$str() + " closed") }
}

fn find(id: i64) -> Session? {
    if id > 0 { return Session(id) }
    nil
}

fn main() -> i64 {
    match find(7) {
        Some(s) => io::println("opened " + s.id.$str()),
        None => io::println("not found"),
    }
    io::println("---")
    match find(-1) {
        Some(s) => io::println("opened " + s.id.$str()),
        None => io::println("not found"),
    }
    0
}""", mode="run", title="The class is released when the Option is"),
    ]))

# ===========================================================================
# 18. Arrays, slices, tuples
# ===========================================================================
SECTIONS.append(Sec(
    "collections", "data types", "Arrays, slices and tuples",
    "An array has a fixed, constant length. A slice borrows part of one. A "
    "tuple groups a fixed set of types.",
    keywords=["array", "slice", "tuple", "index", "length", "bounds", "repeat",
              "slicing", "constant length"],
    items=[
        H("Arrays"),
        P("`[N:T]` is `N` values of `T`, stored inline. The length is part of "
          "the type and must be a constant expression."),
        S("""import std::io

let ROWS: i64 = 3
let COLS: i64 = 4

fn main() -> i64 {
    let literal: [5:i64] = [1, 2, 3, 4, 5]
    let repeated = [7; 4]                      // four sevens
    let computed: [ROWS * COLS:i64] = [0; ROWS * COLS]
    let shifted: [1 << 3:u8] = [0; 8]
    var uninitialised: [3:f64]                 // zeroed
    let nested: [2:[3:i64]] = [[1, 2, 3], [4, 5, 6]]

    io::println(literal[0].$str() + " " + literal[4].$str())
    io::println(repeated.$length())
    io::println(computed.$length())
    io::println(shifted.$length())
    io::println(uninitialised[0])
    io::println(nested[1][2])

    var mutable: [3:i64] = [0, 0, 0]
    mutable[1] = 42
    io::println(mutable[1])
    0
}""", mode="run", title="Literals, repeats, constant lengths, nesting"),
        S("""fn main() -> i64 {
    var count = 4
    let dynamic: [count:i64] = [0; 4]
    0
}""", mode="diag", title="A length that is not constant"),

        H("Bounds checking"),
        P("With `--safety=full` — the default — every index is checked. The "
          "panic names the index and the length."),
        S("""import std::io

fn main() -> i64 {
    let values: [4:i64] = [1, 2, 3, 4]
    var index = 0
    for i in 0..9 { index = i }          // computed, so nothing is folded away
    io::println("reading index " + index.$str())
    io::println(values[index])
    0
}""", mode="panic", title="An out-of-range index aborts"),

        H("Slices"),
        S("""import std::io

// A slice is the shape to write against: one function, arrays of any length,
// and any run inside one.
fn mean(values: [f64]) -> f64 {
    if values.$isEmpty() { return 0.0 }
    var total = 0.0
    for v in values { total += v }
    total / (values.$length() as f64)
}

fn main() -> i64 {
    let week: [7:f64] = [3.0, 4.0, 2.0, 8.0, 6.0, 1.0, 4.0]
    io::println(mean(week).$str())
    io::println(mean(week[0..5]).$str())
    io::println(mean(week[5..]).$str())
    0
}""", mode="run", title="One function, any run of elements"),
        P("`[T]` is a pointer and a length. An array converts to a slice on its "
          "own, and `values[a..b]` borrows part of one."),
        S("""import std::io

fn total(values: [i64]) -> i64 {
    var sum = 0
    for v in values { sum += v }
    sum
}

fn main() -> i64 {
    let all: [6:i64] = [1, 2, 3, 4, 5, 6]

    io::println(total(all))              // the array decays to a slice
    io::println(total(all[0..3]))
    io::println(total(all[3..6]))
    io::println(total(all[0..=2]))       // inclusive
    io::println(total(all[2..]))         // to the end
    io::println(total(all[..2]))         // from the start
    io::println(total(all[..]))          // the whole thing

    let middle = all[1..5]
    io::println(middle.$length())
    io::println(total(middle[1..3]))     // slicing a slice
    io::println(middle.$isEmpty())
    0
}""", mode="run", title="Every slicing form"),
        N("A slice borrows: it does not own the storage, and it does not copy. "
          "Slicing is bounds checked in the same way indexing is.",
          label="Borrowed, not owned"),

        H("Tuples"),
        S("""import std::io

fn divide(a: i64, b: i64) -> (i64, i64) {
    (a / b, a % b)
}

fn main() -> i64 {
    let pair: (i64, String) = (1, "one")
    let triple = (1, 2.5, true)

    io::println(pair.0.$str() + " " + pair.1)
    io::println(triple.1)
    io::println(triple.2)

    let (quotient, remainder) = divide(17, 5)
    io::println(quotient.$str() + " r " + remainder.$str())

    // Nested, and reached by chained indices.
    let nested = ((1, 2), (3, 4))
    io::println(nested.0.1.$str() + " " + nested.1.0.$str())

    var mutable = (0, "start")
    mutable.0 = 9
    mutable.1 = "changed"
    io::println(mutable.0.$str() + " " + mutable.1)
    0
}""", mode="run", title="Building, indexing, destructuring, mutating"),
        N("An array's length is part of its type, so it cannot grow. When you "
          "need one that can, `std::collections::vector` has "
          "`Vector<T>` — same indexing, plus `push` and `pop`.",
          label="Growing one"),
        N("`(T)` in a type is just `T` in parentheses, not a one-element tuple. "
          "`()` is the unit type — the result of a function with no `->`. It "
          "carries no information, so it cannot be bound to a name.",
          label="One element, and none"),

        H("Arrays of aggregates"),
        S("""import std::io

struct Reading { label: String, value: f64 }

fn main() -> i64 {
    let readings: [3:Reading] = [
        Reading { label: "morning", value: 12.5 },
        Reading { label: "noon", value: 21.0 },
        Reading { label: "evening", value: 15.5 },
    ]

    var warmest = readings[0]
    for r in readings {
        if r.value > warmest.value { warmest = r }
    }
    io::println(warmest.label + " " + warmest.value.$str())

    let pairs: [2:(i64, i64)] = [(1, 2), (3, 4)]
    for (a, b) in pairs {
        io::println((a * b).$str())
    }
    0
}""", mode="run", title="Structs and tuples inside an array"),
    ]))

# ===========================================================================
# 19. Strings
# ===========================================================================
SECTIONS.append(Sec(
    "strings", "data types", "Strings and characters",
    "`String` is owned, reference counted and UTF-8. `Character` is one Unicode "
    "scalar. `CString` is a borrowed pointer for talking to C.",
    keywords=["String", "Character", "CString", "concatenate", "length",
              "substring", "utf8", "unicode", "parse"],
    items=[
        H("The three text types"),
        T(["Type", "Owns", "Encoding", "Used for"],
          [["`String`", "yes, reference counted", "UTF-8, NUL-terminated",
            "everything in Rune"],
           ["`Character`", "no, it is a value", "one Unicode scalar",
            "a single character"],
           ["`CString`", "no, it borrows", "bytes to a NUL",
            "passing text to C"]]),
        S("""import std::io

fn main() -> i64 {
    let owned: String = "a Rune String"
    let letter: Character = 'R'
    let foreign: CString = "a C string"

    io::println(owned)
    io::println(letter)
    io::println(foreign)

    // Converting between them.
    io::println(letter.$str())            // Character -> String
    io::println(foreign.$str())           // CString -> String
    io::println(owned.$cstr().$str())      // String -> CString -> String
    0
}""", mode="run", title="One of each, and the conversions"),

        H("Building strings"),
        P("`+` concatenates. Every primitive has `.$str()`, which is what makes "
          "building a message practical."),
        S("""import std::io

fn main() -> i64 {
    let name = "Rune"
    let version = 1
    let ratio = 0.5
    let ready = true
    let initial = 'R'

    let message = name + " v" + version.$str() + " ratio=" + ratio.$str() +
                  " ready=" + ready.$str() + " initial=" + initial.$str()
    io::println(message)

    var built = ""
    for i in 1..=5 { built += i.$str() + "," }
    io::println(built)

    io::println("-".$repeat(20))
    io::println("ab".$repeat(3))
    0
}""", mode="run", title="Concatenation and `.$str()`"),

        H("Inspecting a string"),
        S("""import std::io

fn main() -> i64 {
    let text = "héllo wörld"

    io::println(text.$length())        // bytes
    io::println(text.$charCount())     // Unicode scalars
    io::println(text.$isEmpty())
    io::println("".$isEmpty())

    io::println(text.$substring(0, 6))
    io::println(text.$find("wörld"))   // byte offset, or -1
    io::println(text.$find("absent"))

    io::println(text.$at(1))           // the second character: é
    io::println(text[1])               // the same, as a subscript
    io::println(text.$byteAt(0))       // the raw byte
    io::println(text.$hash() != 0)
    0
}""", mode="run", title="Length, search, and reading characters"),
        T(["Method", "Result", "Notes"],
          [["`length()`", "`i64`", "bytes, not characters"],
           ["`charCount()`", "`i64`", "Unicode scalars"],
           ["`isEmpty()`", "`bool`", ""],
           ["`at(i)`", "`Character`", "the i-th character, counted from the "
            "start; `text[i]` is the same read"],
           ["`byteAt(i)`", "`u8`", "one raw byte"],
           ["`substring(a, b)`", "`String`", "bytes `a` up to `b`"],
           ["`find(needle)`", "`i64`", "byte offset, or `-1`"],
           ["`repeat(n)`", "`String`", ""],
           ["`toInt()`", "`i64?`", "`None` unless the whole string parses"],
           ["`toFloat()`", "`f64?`", "likewise"],
           ["`cstr()`", "`CString`", "borrows this String's bytes"],
           ["`hash()`", "`u64`", "FNV-1a over the bytes"],
           ["`str()`", "`String`", "itself; every type has it"]],
          caption="Every String method"),
        H("Characters: `text[i]` and `for c in text`"),
        P("`text[i]` is the i-th *character*, however wide the ones before it "
          "were — the read `text.$at(i)` makes, spelled as a subscript. It is "
          "a value: a String's characters are not slots, so `text[i] = c` is "
          "refused, and so is `&text[i]`. Each subscript counts from the start "
          "of the UTF-8, so a loop over the characters is written as a loop, "
          "which decodes each one once:"),
        S("""import std::io

fn first_word(text: &String) -> String {
    var word = ""
    for c in text {
        if c == ' ' { break }
        word += c
    }
    word
}

fn main() -> i64 {
    let text = "héllo wörld"
    io::println(first_word(&text))
    io::println(text[1].$str() + text[7].$str())    // éö

    var count = 0
    for c in text { count += 1 }
    io::println(count.$str() + " characters in " + text.$length().$str() + " bytes")

    var reversed = ""
    for c in "abc" { reversed = c.$str() + reversed }
    io::println(reversed)
    0
}""", mode="run", title="Reading characters"),
        P("A `String` is a `Sequence` whose items are `Character`s, so the "
          "iterator adaptors apply to it as they do to a `Vector`: "
          "`text.iterate().map(...)`, `.filter(...)`, `.count()`. Slicing "
          "with `[a..b]` is not offered — a range of *bytes* would cut a "
          "character in half, and a range of characters would have to count "
          "its way in — `$substring(a, b)` takes byte offsets and says so."),
        S("""fn main() -> i64 {
    var text = "abc"
    text[0] = 'x'
    0
}""", mode="diag", title="Not a slot"),

        H("Parsing"),
        S("""import std::io

fn main() -> i64 {
    io::println("42".$toInt().or(-1))
    io::println("-17".$toInt().or(-1))
    io::println("2.5".$toFloat().or(0.0))
    io::println("nope".$toInt().or(-1))
    io::println("42x".$toInt().isNil())      // the whole string must parse

    match "123".$toInt() {
        Some(n) => io::println("parsed " + n.$str()),
        None => io::println("not a number"),
    }
    0
}""", mode="run", title="`toInt` and `toFloat` return an Option"),

        H("Characters"),
        S("""import std::io

fn isVowel(c: Character) -> bool {
    c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u'
}

fn main() -> i64 {
    io::println(isVowel('e'))
    io::println(isVowel('z'))
    io::println('a' < 'b')
    io::println('A' as i64)
    io::println(97 as Character)

    // Walking a string by character.
    let text = "hello"
    var vowels = 0
    var i = 0
    while i < text.$length() {
        if isVowel(text.$at(i)) { vowels += 1 }
        i += 1
    }
    io::println(vowels)
    0
}""", mode="run", title="Comparing, converting, and walking"),

        H("Normalisation and collation"),
        P("The same text can be written more than one way. `é` is either one "
          "code point or two — `e` followed by a combining acute — and `==` "
          "compares bytes, so it says they differ. `std::text` is where the "
          "Unicode answers live."),
        S(r"""import std::io
import std::text

fn main() -> i64 {
    let composed = "é"            // U+00E9
    let decomposed = "e\u{301}"   // e + combining acute

    io::println((composed == decomposed).$str())            // bytes differ
    io::println(text::equal(composed, decomposed).$str())   // same text

    let nfd = text::normalize(composed, text::Form::Decomposed)
    let nfc = text::normalize(decomposed, text::Form::Composed)
    io::println(nfd.$charCount().$str() + " " + nfc.$charCount().$str())
    io::println((nfc == composed).$str())
    0
}""", mode="run", title="Two spellings, one text"),
        T(["Member", "Signature", "Does"],
          [["`Form`", "`enum { Decomposed, Composed }`", "NFD or NFC"],
           ["`normalize`", "`(text: String, form: Form) -> String`", ""],
           ["`equal`", "`(a: String, b: String) -> bool`", "same text, either "
            "spelling"],
           ["`equalIgnoringCase`", "`(a: String, b: String) -> bool`", "and "
            "ignoring case"],
           ["`compare`", "`(a: String, b: String) -> i64`", "orders the way a "
            "reader would"],
           ["`combiningClass`", "`(c: Character) -> i64`", "0 for a starter"],
           ["`isCombining`", "`(c: Character) -> bool`", "a mark that attaches "
            "to what precedes it"],
           ["`foldCase`", "`(c: Character) -> Character`", "simple folding, "
            "one code point to one"],
           ["`codePoints`", "`(text: String) -> CodePoints`", "the code points "
            "in order"],
           ["`fromCodePoints`", "`(cps: CodePoints) -> String`", "and back"]]),
        P("`compare` works in three passes: base letters first with accents "
          "and case set aside, then the accents, then the case. That is what "
          "puts a word's accented and capitalised variants next to it instead "
          "of scattering them by byte value."),
        S("""import std::io
import std::text

fn main() -> i64 {
    var words: [6:String] = ["zebra", "Ápple", "apple", "Banana", "äpple", "APPLE"]
    var i = 1
    while i < 6 {
        var j = i
        while j > 0 {
            if text::compare(words[j], words[j - 1]) < 0 {
                let hold = words[j]
                words[j] = words[j - 1]
                words[j - 1] = hold
            }
            j -= 1
        }
        i += 1
    }
    var out = ""
    for w in words { out += w + " " }
    io::println(out)
    0
}""", mode="run", title="Sorting by the collation, not by bytes"),
        N("This is a *root* ordering, not a locale-aware one. It does not know "
          "that Swedish sorts `ä` after `z`, and it is not the Unicode "
          "Collation Algorithm — there is no DUCET table behind it. Case "
          "folding is simple, so `ß` stays one character rather than becoming "
          "`ss`.", label="What it is not", tone="warn"),
        N("Hangul is not in the tables: its composition and decomposition are "
          "arithmetic, and `std::text` does that directly. Everything else "
          "comes from the Unicode Character Database, generated by "
          "`runtime/tools/gen_unicode.py`.", label="Where the data comes from"),

        H("Splitting, trimming and replacing"),
        P("The everyday half of `std::text`. These work on bytes rather than "
          "code points, and are built from the compiler's own `$find`, "
          "`$substring` and `$length` — which makes them exact for the ASCII "
          "delimiters a separator, a newline or a piece of punctuation almost "
          "always is, and makes them cheap. Anything that has to be right "
          "about accents or case belongs above, with the normalisation."),
        T(["Function", "Signature", "Does"],
          [["`startsWith` / `endsWith`", "`(String, String) -> bool`",
            "an empty affix always matches"],
           ["`contains`", "`(String, String) -> bool`", "anywhere in it"],
           ["`find`", "`(String, String) -> i64?`",
            "where — `nil` rather than `$find`'s -1"],
           ["`withoutPrefix` / `withoutSuffix`", "`(String, String) -> String`",
            "removed if present, unchanged if not"],
           ["`trim` / `trimStart` / `trimEnd`", "`(String) -> String`",
            "whitespace off the ends"],
           ["`split`", "`(String, String) -> Vector<String>`",
            "cut at every occurrence; adjacent separators give empty pieces"],
           ["`splitLines`", "`(String) -> Vector<String>`",
            "at `\\n`, dropping a trailing `\\r`; no empty final line"],
           ["`replace`", "`(String, String, String) -> String`",
            "every occurrence; the replacement is not searched again"],
           ["`join`", "`(Vector<String>, String) -> String`",
            "the other half of `split`"],
           ["`padStart` / `padEnd`", "`(String, i64, Character) -> String`",
            "to a width; longer text is returned rather than cut"],
           ["`lower` / `upper`", "`(String) -> String`",
            "case, **ASCII only** — every other byte is left as it is"]]),
        N("`lower` and `upper` deliberately stop at ASCII. Real case mapping "
          "depends on the language (Turkish dotless ı, German ß, Greek "
          "final sigma) and can change a string's length, so a function that "
          "quietly did the wrong thing for those would be worse than one that "
          "says what it does. Use them for keywords, extensions and protocol "
          "tokens; for anything a person reads, normalise first.",
          label="Why only ASCII"),
        S("""import std::io
import std::text

fn main() -> i64 {
    let line = "  name, age , city  "

    io::println("[" + text::trim(line) + "]")
    for field in text::split(text::trim(line), ",") {
        io::println("<" + text::trim(field) + ">")
    }

    io::println(text::replace("banana", "a", "o"))
    io::println(text::join(text::split("x;y;z", ";"), "-"))
    io::println(text::withoutSuffix("report.txt", ".txt"))
    io::println(text::padStart("7", 3, '0'))

    // `$find` answers -1 for "nowhere". `text::find` answers `nil`, which
    // cannot be mistaken for a position.
    io::println((text::find("hello", "llo") ?? -1).$str())
    io::println((text::find("hello", "zzz") ?? -1).$str())
    0
}""", mode="run", title="Taking a line apart"),

        H("Comparison and sorting"),
        S("""import std::io

fn main() -> i64 {
    io::println("abc" == "abc")
    io::println("abc" != "abd")
    io::println("abc" < "abd")        // byte-wise, so ASCII order
    io::println("Z" < "a")            // uppercase sorts first
    io::println("ab" < "abc")         // a prefix sorts first

    var names: [4:String] = ["pear", "apple", "fig", "date"]
    // A simple insertion sort, to show the comparisons at work.
    for i in 1..4 {
        var j = i
        while j > 0 {
            if names[j] < names[j - 1] {
                let hold = names[j]
                names[j] = names[j - 1]
                names[j - 1] = hold
            }
            j -= 1
        }
    }
    var out = ""
    for n in names { out += n + " " }
    io::println(out)
    0
}""", mode="run", title="Ordering strings"),
    ]))


# ===========================================================================
# Formatting
# ===========================================================================
SECTIONS.append(Sec(
    "formatting", "data types", "Formatting and printing",
    "`println!` writes a formatted line, `print!` writes without one, and "
    "`format!` builds the `String` both of them print. The format string is "
    "read at compile time, so what it asks for is checked then.",
    [
        H("Placeholders"),
        P("Each `{}` takes the next argument. `{{` and `}}` stand for literal "
          "braces. None of the three needs an import: their bodies name "
          "`std::io` in full."),
        S('''fn main() -> i64 {
    let name = "world"
    let count = 3

    println!("hello {}", name)
    println!("{} + {} = {}", 1, 2, 1 + 2)
    println!("no placeholders at all")
    println!()                          // a blank line

    // A placeholder may name a variable directly, or an argument's position.
    println!("{count} of {name}")
    println!("{1} then {0}", "a", "b")

    // `{{` and `}}` are the literal braces.
    println!("{{not a placeholder}}")

    // print! is the same without the newline.
    print!("a")
    print!("b")
    println!()

    // format! returns the String instead of printing it.
    let line = format!("{} items", count)
    println!("{}", line.$length())
    0
}''', mode="run", title="What a placeholder can hold"),
        T(["Placeholder", "Takes"],
          [["`{}`", "the next argument in turn"],
           ["`{0}`, `{1}`", "the argument at that position"],
           ["`{name}`", "the variable `name`, from where the macro was used"],
           ["`{{`, `}}`", "a literal `{` or `}`"]]),

        H("Format options"),
        P("A `:` inside a placeholder is followed by how to render it: width "
          "and alignment, a number of places, or a radix."),
        S('''fn main() -> i64 {
    // Width, and which side the text is held to. A character before the
    // alignment is the fill.
    println!("[{:>8}] [{:<8}] [{:^8}] [{:*^9}]", "ab", "ab", "ab", "ab")

    // `0` fills with zeros, and keeps the sign in front of them.
    println!("[{:08}] [{:08}]", 42, -42)

    // Places after the point, alone and with a width.
    println!("{:.4}  [{:10.2}]  [{:08.2}]", 3.14159265, 3.14159, -3.14159)

    // Radix. `#` adds the prefix, and zeros go inside it.
    println!("{:x} {:X} {:b} {:o}", 255, 255, 10, 64)
    println!("{:#x} {:#b} {:#06x}", 255, 10, 255)

    // An explicit sign on anything that has none of its own.
    println!("{:+} {:+} {:+.2}", 5, -5, 1.5)
    0
}''', mode="run", title="Every option, at work"),
        T(["Option", "Means", "Example"],
          [["`<` `^` `>`", "align left, centre, right", "`{:>8}`"],
           ["*char* before the align", "what to pad with", "`{:*^9}`"],
           ["`0`", "pad with zeros, inside the sign and any `0x`", "`{:08}`"],
           ["*number*", "the minimum width, in characters", "`{:8}`"],
           ["`.`*number*", "places after the point", "`{:.2}`"],
           ["`x` `X`", "hexadecimal, lower or upper case", "`{:x}`"],
           ["`b` `o`", "binary, octal", "`{:b}`"],
           ["`#`", "the `0x`, `0b` or `0o` prefix", "`{:#x}`"],
           ["`+`", "a leading `+` when there is no sign", "`{:+}`"]]),
        P("The order inside a placeholder is "
          "`{[argument][:[[fill]align][+][#][0][width][.places][kind]]}`. "
          "Width counts characters rather than bytes, so a column of accented "
          "text lines up."),
        N("A number of places and a radix cannot both apply, and neither can "
          "a kind the list above does not have. Both are errors at the "
          "`format!`, not surprises at run time.",
          label="Checked where it is written"),

        H("What can be formatted"),
        P("`{}` renders through `io::Display`, so a type gains formatting by "
          "being bound to it — the same thing that lets `io::println` take it. "
          "There is no second mark to implement."),
        S('''import std::io

struct Money { cents: i64 }

bind io::Display to Money {
    // A `display` body may itself use `format!`.
    fn display(&self) -> String {
        format!("${}.{:02}", self.cents / 100, self.cents % 100)
    }
}

fn main() -> i64 {
    let price = Money { cents: 1999 }
    println!("{}", price)
    println!("[{:>10}]", price)     // width applies to any Display
    0
}''', mode="run", title="A type that formats"),

        H("When arguments are evaluated"),
        P("Each argument is evaluated once, in the order it is written — "
          "whatever order the placeholders read them in. Where the "
          "placeholders happen to use each argument once and in order, they "
          "are spliced where they are used; otherwise they are bound first, "
          "which is what keeps `{0} {0}` from calling twice."),
        S('''import std::io

global var calls = 0
fn tick() -> i64 { calls += 1; calls }

global var order = ""
fn tag(name: String, value: i64) -> i64 { order += name; value }

fn main() -> i64 {
    println!("{0} {0} {0}", tick())
    println!("tick ran {} time(s)", calls)

    // Read second-then-first, but still evaluated first-then-second.
    println!("{1} {0}", tag("a", 1), tag("b", 2))
    println!("evaluated: {}", order)
    0
}''', mode="run", title="Once each, in the order written"),

        H("The pieces underneath"),
        P("A placeholder expands into a call from `std::fmt`, which is an "
          "ordinary module: the functions are there to be used directly when "
          "a format string is not the clearest way to say something."),
        T(["Placeholder", "Expands to"],
          [["`{}`", "`std::fmt::show(x)`"],
           ["`{:.2}`", "`std::fmt::fixed(x, 2)`"],
           ["`{:#x}`", "`std::fmt::radix(x, 16, false, true)`"],
           ["`{:+}`", "`std::fmt::plus(std::fmt::show(x))`"],
           ["`{:*^9}`", "`std::fmt::pad(std::fmt::show(x), 9, '^', '*')`"]]),
        N("An error inside an expansion shows what the macro stood for, so a "
          "type mismatch in a `{:.2}` names `fmt::fixed` and the argument it "
          "was given. See **Macros**.",
          label="Errors point back"),
    ],
    keywords=["format", "println", "print", "formatting", "placeholder",
              "width", "align", "pad", "precision", "hex", "binary", "octal",
              "radix", "fmt", "Display", "interpolation", "printf"]))


# ===========================================================================
# Memory and reference counting
# ===========================================================================
SECTIONS.append(Sec(
    "memory", "lifetime", "Memory and reference counting",
    "Class instances are reference counted. The compiler inserts every retain "
    "and release itself, at the points a careful C programmer would have "
    "written them, and a debug build tells you if any object outlived the "
    "program.",
    [
        H("What is counted, and what is not"),
        P("Only *classes* carry a reference count. Integers, floats, `bool`, "
          "`Character`, tuples, structs, arrays and enums are values: assigning "
          "one copies it, and it dies with the scope that named it. `String` is "
          "counted too, but its buffer is an implementation detail you never "
          "see."),
        T(["Kind", "Storage", "Assignment", "Counted"],
          [["`i64`, `f64`, `bool`, `Character`", "inline", "copies", "no"],
           ["`struct`", "inline", "copies every field", "no"],
           ["`enum`", "inline (tag + payload)", "copies the payload", "no"],
           ["`[N:T]`", "inline", "copies every element", "no"],
           ["tuple", "inline", "copies every member", "no"],
           ["`class`", "heap", "shares the same object", "yes"],
           ["`String`", "heap", "shares the same buffer", "yes"]],
          caption="A struct holding a class field is itself a value, but "
                  "copying it retains the class the field points at."),

        H("Sharing an object"),
        P("Two bindings that name the same instance see the same object. The "
          "count is how many bindings are alive, not how many were ever made."),
        S("""import std::io
import std::process

class Node {
    label: String
    fn init(self, label: String) { self.label = label }
    fn deinit(self) { io::println("releasing " + self.label) }
}

fn main() -> i64 {
    // Measure before printing: building the message would itself allocate.
    let before = process::liveObjectCount()
    io::println("live before: " + before.$str())
    {
        let first = Node("root")
        let second = first        // no copy: both name one object
        second.label = "renamed"  // so this is visible through `first`
        io::println(first.label)
        let inside = process::liveObjectCount()
        io::println("live inside: " + inside.$str())
    }
    // Both bindings went out of scope, so the object is gone.
    let after = process::liveObjectCount()
    io::println("live after: " + after.$str())
    0
}""", mode="run", title="One object, two names"),
        N("Two live objects inside the scope, not one: the `Node` and the "
          "`String` its `label` field holds. Strings are counted as well.",
          label="Why 2"),

        H("Deinitialisers"),
        S("""import std::io

// `deinit` runs the moment the last reference does — not at some later
// collection — so closing happens where you can see it.
class Connection {
    name: String
    fn init(self, name: String) {
        self.name = name
        io::println("open " + name)
    }
    fn deinit(self) { io::println("close " + self.name) }
}

fn useOne() {
    let c = Connection("inner")
    io::println("  working with " + c.name)
}

fn main() -> i64 {
    let outer = Connection("outer")
    useOne()
    io::println("back in main")
    0
}""", mode="run", title="Closing happens where you can see it"),
        P("`deinit` runs when the last reference goes away. It runs before the "
          "object's own fields are released, and before the superclass's "
          "`deinit`, so a subclass always tears down before the base it was "
          "built on."),
        S("""import std::io

class Resource {
    name: String
    fn init(self, name: String) { self.name = name }
    fn deinit(self) { io::println("close " + self.name) }
}

class Pooled: Resource {
    index: i64
    fn init(self, name: String, index: i64) {
        super.init(name: name)
        self.index = index
    }
    fn deinit(self) { io::println("return slot " + self.index.$str()) }
}

fn main() -> i64 {
    let p = Pooled("socket", 3)
    io::println("using " + p.name)
    0
}""", mode="run", title="Teardown runs subclass first"),
        N("A `deinit` takes no parameters, returns nothing, and cannot be "
          "called by hand. There is no way to run one early — drop the last "
          "reference instead.", label="deinit is not a method"),

        H("Values have destructors too"),
        P("A struct or an enum may declare a `deinit`, and it runs when the "
          "value it lives in is destroyed rather than when a count reaches "
          "zero. That is what lets a value own something reference counting "
          "cannot see: a file descriptor, a lock, a handle from C. The two "
          "kinds differ only in what decides the moment — a class's runs when "
          "the last reference goes, a value's when the binding does. See "
          "[Structs](#structs) for the whole of it, and "
          "[Safety levels](#safety) for what the compiler checks."),
        S("""import std::io

struct Slot { pub n: i64 }
extend Slot { fn deinit(&self) { io::println("released " + self.n.$str()) } }

class Holder {
    slot: Slot
    fn init(self) { self.slot = Slot { n: 7 } }
    fn deinit(self) { io::println("holder going") }
}

fn main() -> i64 {
    { let h = Holder() }            // the class first, then its field
    { let s = Slot { n: 1 } }       // the value on its own
    0
}""", mode="run", title="Both kinds, side by side"),
        P("A value that owns something is *moved* rather than copied when it "
          "is handed on, so exactly one binding owns it at a time. A class "
          "reference is still shared as it always was; ownership of a value "
          "and counting of a reference are separate questions, and a struct "
          "that has both answers both."),

        H("Counting that is not emitted"),
        P("At `--safety full` the ownership pass works out which locals never "
          "leave the scope that declared them, and a class built for one of "
          "those needs no counting: nothing else can reach it, so the "
          "allocation's own reference *is* the binding's and the scope hands "
          "it back on the way out. Nothing has to be written to get this and "
          "nothing observable changes — `process::liveObjectCount()` agrees "
          "either way. What changes is that the retain, the temporary slot "
          "and the paired release are simply not emitted."),
        N("Anything the pass cannot follow escapes: a capture, a raw-pointer "
          "cast, a store into a field, a call taking it by value. So this "
          "applies where the whole story is visible in one body, and nowhere "
          "else.", label="Only where it is provable"),

        H("Cycles are refused, not collected"),
        P("Reference counting frees an object when the last reference to it "
          "goes. A ring of objects holding each other never reaches zero, so "
          "it never gets freed — the one way a fully safe program could still "
          "leak. There is no cycle collector; instead, at `--safety full` the "
          "compiler refuses the shape that allows one."),
        S("""class Parent {
    child: Child?
    fn init(self) { self.child = nil }
}

class Child {
    owner: Parent?      // strong both ways: this is the shape that leaks
    fn init(self) { self.owner = nil }
}

fn main() -> i64 { 0 }""", mode="warn", title="A ring the compiler reports"),
        P("The check follows what an object *owns* — its fields, and through "
          "structs, tuples, arrays, enum payloads and Options. It stops at "
          "anything that does not keep its target alive, so a `weak` field, a "
          "borrow or a raw pointer is not an edge."),
        T(["Shape", "At `--safety full`"],
          [["`A.b: B` and `B.a: A`", "reported"],
           ["`N.me: N?`", "reported"],
           ["a ring of three", "reported"],
           ["through a struct field", "reported"],
           ["through an enum payload", "reported"],
           ["through an array element", "reported"],
           ["`weak` on either edge", "silent"],
           ["`Unique` on the owning edge", "silent"],
           ["`&T` or `*T` — a borrow owns nothing", "silent"]]),
        S("""import std::io
import std::process

class Parent {
    name: String
    child: Child?
    fn init(self, name: String) { self.name = name; self.child = nil }
    fn deinit(self) { io::println("drop parent") }
}

class Child {
    weak owner: Parent?     // one weak edge is enough
    fn init(self) { self.owner = nil }
    fn deinit(self) { io::println("drop child") }
}

fn main() -> i64 {
    let before = process::liveObjectCount()
    {
        let p = Parent("configuration")
        let c = Child()
        p.child = c
        c.owner = p
        match c.owner {
            Some(up) => io::println("child sees parent: " + up.name),
            None => io::println("orphaned"),
        }
    }
    // Bound before printing: calling `liveObjectCount()` inside a
    // concatenation counts the half-built string too.
    let after = process::liveObjectCount()
    io::println("balanced " + (before == after).$str())
    0
}""", mode="run", title="One weak edge, and it frees"),
        N("The rule looks at **types**, not at the objects you actually build. "
          "A forward-linked `class Node { next: Node? }` is reported even when "
          "you only ever build a chain, because nothing in the type stops the "
          "last node pointing back at the first. That is why it is a warning "
          "and not a refusal: a type that *can* loop is not a program that "
          "does, and refusing the shape would refuse every owning list and "
          "tree with it.",
          label="It reports on types, not on programs", tone="warn"),
        P("When a structure should be incapable of looping at all, do not "
          "argue with the warning — own it through `Unique`, below, and there is "
          "nothing left to warn about."),
        N("One thing the rule cannot see: a closure's captures are not part of "
          "its type, so a class holding a closure that captures that same "
          "class is a ring the check will not catch. The exit report still "
          "finds it.", label="The remaining hole", tone="warn"),

        H("`Unique`: one owner, so no ring"),
        P("`weak` breaks a cycle after the fact — you have to see it coming "
          "and pick an edge to weaken. `Unique` makes one impossible instead."),
        P("A `Unique<T>` is a reference to a class instance with **exactly one "
          "owner**. It is represented like any other class reference and costs "
          "nothing extra at run time; what makes it unique is that the "
          "compiler will not duplicate it. It can be *moved*, with `.$move()`, or "
          "*borrowed*, with `&`. There is no third thing. Closing a ring needs "
          "a second reference to the same object, and nobody can produce one."),
        S("""import std::io
import std::process

class Node {
    pub value: i64
    pub next: Unique<Node>?           // this node owns the rest of the chain
    fn init(self, value: i64) { self.value = value }
    fn deinit(self) { io::println("releasing " + self.value.$str()) }
}

/// Borrowing reaches the object without becoming a second owner.
fn total(n: &Node) -> i64 {
    var sum = n.value
    match n.next {
        Some(c) => sum += total(&c),
        None => {}
    }
    sum
}

fn main() -> i64 {
    let before = process::liveObjectCount()
    {
        let head: Unique<Node> = Node(1)
        let second: Unique<Node> = Node(2)
        second.next = Node(3)          // fresh: nothing to move from
        head.next = second        // `second` is unusable from here
        io::println("total " + total(&head).$str())
    }
    // Bound before printing: calling `liveObjectCount()` inside a
    // concatenation counts the half-built string too.
    let after = process::liveObjectCount()
    io::println("balanced " + (before == after).$str())
    0
}""", mode="run", title="A chain that cannot cycle"),
        P("Releasing the head releases the link it owns, and so on down the "
          "chain — which is why the whole thing goes at once, and why the "
          "count comes back to where it started."),
        N("`liveObjectCount()` counts what is alive *at the instant it is "
          "called*. A string literal costs nothing — literals are interned "
          "into one immortal object each, which is never counted — but the "
          "**result** of a concatenation is a real object, so calling the "
          "count in the middle of building a longer message counts that "
          "partial result too and reads exactly like a leak. Bind the count "
          "first, as above, and compare the bindings.",
          label="Measure it outside the string", tone="warn"),
        N("A fresh construction may become a `Unique` without any transfer, because "
          "nothing else refers to it yet. Everything else has to be handed "
          "over explicitly.", label="Where a `Unique` comes from"),
        P("Four things are refused, all as `E0239`. Between them they are "
          "every way a second reference could have appeared:"),
        S("""class Node {
    pub value: i64
    pub next: Unique<Node>?
    pub sneaky: Node?
    fn init(self, value: i64) { self.value = value }
}

fn main() -> i64 {
    let a: Unique<Node> = Node(1)
    let b: Unique<Node> = Node(2)
    a.next = b
    let seen = b.value            // 'b' has been moved out of
    let c: Unique<Node> = Node(3)
    let d: Unique<Node> = c          // cannot copy into this initialiser
    let e: Node = c               // cannot copy into a counted reference
    a.sneaky = c                  // ... which is what would close the ring
    0
}""", mode="diag", title="The four ways it says no"),
        P("Returning one is a transfer, so it is written out. A chain is built "
          "from the tail forwards, because each node owns the one after it: "
          "there is no way to keep a cursor on the end and still hand the "
          "whole thing back."),
        S("""import std::io

class Node {
    pub value: i64
    pub next: Unique<Node>?
    fn init(self, value: i64) { self.value = value }
}

fn buildChain(count: i64) -> Unique<Node> {
    var head: Unique<Node> = Node(count - 1)
    var i = count - 2
    while i >= 0 {
        let node: Unique<Node> = Node(i)
        node.next = head      // the new node takes the chain over
        head = node           // and becomes the chain
        i -= 1
    }
    head
}

fn walk(n: &Node) -> String {
    var out = n.value.$str() + " "
    match n.next {
        Some(c) => out += walk(&c),
        None => {}
    }
    out
}

fn main() -> i64 {
    let chain = buildChain(5)
    io::println(walk(&chain))
    0
}""", mode="run", title="Building one, tail first"),
        N("Assigning to a local that was moved out of gives it something to "
          "hold again, which is what makes `head = node` work on the "
          "second turn of that loop.", label="A moved-from name can be reused"),
        T(["Rule", "Why"],
          [["`Unique` is not an explicit generic argument",
            "`Unique<Node>?` is fine — that is `Option`, the compiler's own — but "
            "`Handle<Unique<Node>>` is refused, because a generic written for a "
            "copyable `T` would copy this one"],
           ["move tracking is flow-insensitive",
            "a move in one branch of an `if` marks the local moved after the "
            "`if` on every path; it refuses some valid programs, and accepts "
            "no invalid ones"],
           ["you cannot move out of a field",
            "the field would be left holding nothing, and only an optional "
            "field can say that — assign a replacement instead"],
           ["`Unique` needs a class",
            "only a class instance is counted, so only one has an owner to "
            "hand over"]]),

        H("`weak`, in detail"),
        P("Reference counting cannot collect a cycle: two objects that point at "
          "each other keep each other's count at one forever. Break the cycle "
          "by marking the back-reference `weak`. A weak field does not raise the "
          "count, and it is set to `nil` the moment its target is released — so "
          "reading it can never hand you a dead object."),
        S("""import std::io
import std::process

class Parent {
    name: String
    child: Child?
    fn init(self, name: String) { self.name = name; self.child = nil }
    fn deinit(self) { io::println("drop parent") }
}

class Child {
    // Without `weak` this pair would keep each other alive forever.
    weak owner: Parent?
    fn init(self) { self.owner = nil }
    fn deinit(self) { io::println("drop child") }
}

fn main() -> i64 {
    {
        let p = Parent("configuration")
        let c = Child()
        p.child = c
        c.owner = p          // weak: does not retain
        match c.owner {
            Some(up) => io::println("child belongs to " + up.name)
            None => io::println("orphaned")
        }
    }
    let remaining = process::liveObjectCount()
    io::println("live after scope: " + remaining.$str())
    0
}""", mode="run", title="A weak back-reference"),
        S("""import std::io

class Cache {
    label: String
    fn init(self, label: String) { self.label = label }
}

class Watcher {
    weak target: Cache?
    fn init(self) { self.target = nil }
}

fn main() -> i64 {
    let w = Watcher()
    {
        let c = Cache("hot")
        w.target = c
        io::println("while alive: " + w.target.hasValue().$str())
    }
    // `c` is gone, so the weak slot was zeroed for us.
    io::println("after release: " + w.target.hasValue().$str())
    0
}""", mode="run", title="A weak field zeroes itself"),
        N("`weak` needs an optional type: the field has to be able to hold "
          "`nil`, because that is what it becomes.",
          label="weak implies optional", tone="warn"),

        H("Leak reporting"),
        P("At `--safety full` — the default — the runtime counts live objects at "
          "exit and reports anything left over on stderr. It is not a garbage "
          "collector; it is a check that the counts balanced."),
        S("""import std::io

class Ring {
    next: Ring?
    fn init(self) { self.next = nil }
}

fn main() -> i64 {
    let a = Ring()
    let b = Ring()
    a.next = b
    b.next = a      // strong both ways: neither can ever reach zero
    io::println("built a cycle")
    0
}""", mode="leak", title="A strong cycle is reported at exit",
          safety="minimal"),
        N("`process::liveObjectCount()` is the same counter the leak report "
          "uses. It is a legitimate way to assert in a test that a data "
          "structure released everything it should.",
          label="Checking it yourself"),

        H("Where the counting happens"),
        P("The convention is worth knowing even though you never write it. A "
          "function returns objects **owned** — the caller inherits a count. "
          "Reading a field or a variable produces a **borrowed** value, which "
          "the compiler retains only if it needs to outlive the expression. "
          "Assigning to a field retains the new value before releasing the old "
          "one, so `x.f = x.f` is safe. Locals are released in reverse "
          "declaration order at the end of their scope, and globals in reverse "
          "declaration order after `main` returns."),
        N("`mem::Handle<T>` puts any single value on the heap under exactly "
          "this scheme — it is a class, so a handle is counted like anything "
          "else, and the value goes when the last handle does. Reach for it "
          "rather than an allocator.", label="One value on the heap"),

        H("Three owning pointers, and what tells them apart"),
        P("`std::mem` offers three. They differ in one thing only — how many "
          "places may own the value — and that decides everything else about "
          "them."),
        T(["Type", "Owners", "Costs", "Made by"],
          [["`Handle<T>`", "as many as share the handle", "a class, so a "
            "reference count", "`mem::of(v)`"],
           ["`Box<T>`", "exactly one", "one machine word, no bookkeeping",
            "`mem::boxed(v)`"],
           ["`Rc<T>`", "as many as ask, each by cloning", "two counts in the "
            "block it allocates", "`mem::shared(v)`"],
           ["`Weak<T>`", "none — it watches", "a share of the same block",
            "`rc.downgrade()`"]],
          caption="A `Box` is a value with a destructor, so it is moved "
                  "rather than copied, and the one place holding it frees it."),
        P("`Rc` keeps its counts in fields of its own rather than leaving them "
          "to the compiler, which is what makes it mean the same thing under "
          "`--memory zombie`, where nothing is counted for you. It is the way "
          "two places share a value there."),
        S("""import std::io
import std::mem

struct Point { x: i64, y: i64 }

fn main() -> i64 {
    // One owner, moved rather than copied.
    var b = mem::boxed(Point { x: 1, y: 2 })
    b.x = 10
    io::println(b.x.$str())

    // As many owners as ask, each by cloning.
    let a = mem::shared("hello")
    let second = a.$clone()
    io::println(a.strongCount().$str())     // 2
    io::println(*second)

    // A watcher that does not keep it alive.
    var watcher: mem::Weak<String>
    {
        let held = mem::shared("gone soon")
        watcher = held.downgrade()
        io::println(watcher.isAlive().$str())
    }
    io::println(watcher.isAlive().$str())
    0
}""", mode="run", title="One owner, several owners, and a watcher"),

        H("Reaching through a stand-in"),
        P("A pointer that held its value at arm's length would be tedious to "
          "use, so `.` reaches through it. What makes a type one of these is "
          "that it **lends**: a `look(&self) -> &T from self` to read the "
          "value where it lies, and a `touch(&var self) -> &var T from self` "
          "to write it. `Handle`, `Box`, `Rc` and the borrows `Checked` hands "
          "out all have them, and so may anything you write."),
        S("""import std::io
import std::mem
import std::collections::vector

struct Point { x: i64, y: i64 }

extend Point {
    fn sum(&self) -> i64 { self.x + self.y }
    fn shift(&var self, by: i64) { self.x += by; self.y += by }
}

fn main() -> i64 {
    var b = mem::boxed(Point { x: 1, y: 2 })
    io::println(b.x.$str())         // through `look`
    io::println(b.sum().$str())     // through `look`
    b.x = 10                        // through `touch`
    b.shift(5)                      // through `touch`: `shift` takes `&var self`
    io::println(b.y.$str())

    // Whatever is inside keeps its own methods, however deep.
    var v = mem::boxed(vector::Vector<i64>())
    v.push(1)
    v.push(2)
    io::println(v.length().$str())
    0
}""", mode="run", title="`.` goes through to the value"),
        N("It can never hide anything. The reach-through only happens once a "
          "member has *not* been found on the stand-in itself, so "
          "`b.duplicate()` is still the box's own and only a name the box does "
          "not have goes through.", label="It cannot shadow"),
        S("""import std::mem

struct Point { x: i64, y: i64 }

fn main() -> i64 {
    var shared = mem::shared(Point { x: 1, y: 2 })
    shared.x = 5
    0
}""", mode="diag", title="An `Rc` lends for reading only"),
        P("An `Rc` has no `touch`, because several owners writing at once is "
          "the thing it exists to make impossible. A shared value that has to "
          "change keeps a `mem::Checked<T>` inside, which decides at run time "
          "that a write is the only one out."),
        S("""import std::io
import std::mem

fn main() -> i64 {
    let cell = mem::shared(mem::Checked<i64>(0))
    let alias = cell.$clone()
    { var w = alias.look().borrowVar(); *w = 42 }
    io::println((*cell.look().borrow()).$str())
    0
}""", mode="run", title="Changing what is shared"),
    ],
    keywords=["arc", "retain", "release", "weak", "cycle", "deinit", "leak",
              "reference counting", "memory", "strong cycle", "E0235",
              "no leaks", "Box", "Rc", "Weak", "Handle", "smart pointer",
              "boxed", "shared", "downgrade", "upgrade", "look", "touch"]))


# ===========================================================================
# Pointers and borrows
# ===========================================================================
SECTIONS.append(Sec(
    "pointers", "indirection", "Pointers, borrows and slices",
    "Four kinds of indirection, in order of how much the compiler will do for "
    "you: shared borrows, mutable borrows, slices, and raw pointers.",
    [
        T(["Written", "Means", "Checked", "Needs `unsafe`"],
          [["`&T`", "shared borrow, read only", "yes", "no"],
           ["`&var T`", "mutable borrow", "yes", "no"],
           ["`[T]`", "slice: a borrow of a run of elements", "yes", "no"],
           ["`*T`", "raw pointer, read only", "no", "yes"],
           ["`*var T`", "raw mutable pointer", "no", "yes"]]),

        H("Shared and mutable borrows"),
        P("A borrow lets a function reach a value without taking it. `&value` "
          "creates one, `*p` reaches through it, and field and method access "
          "dereference on their own — so the `*` is only needed when the whole "
          "value is the target."),
        S("""import std::io

struct Counter { value: i64, step: i64 }

/// Reads through a shared borrow; the caller keeps the value.
fn peek(c: &Counter) -> i64 {
    c.value                 // no `*` needed for a field
}

/// Writes through a mutable borrow.
fn advance(c: &var Counter) {
    (*c).value += (*c).step
}

fn doubled(n: &i64) -> i64 {
    *n * 2                  // a scalar, so the `*` is required
}

fn main() -> i64 {
    var counter = Counter { value: 10, step: 4 }
    io::println("before: " + peek(&counter).$str())
    advance(&var counter)
    advance(&var counter)
    io::println("after:  " + peek(&counter).$str())
    io::println("doubled step: " + doubled(&counter.step).$str())
    0
}""", mode="run", title="Borrowing a struct"),
        N("`&var` needs a mutable place. Borrowing a `let` binding mutably is "
          "an error, which is how the compiler knows a shared borrow cannot be "
          "written through.", label="Mutability is checked", tone="warn"),
        S("""import std::io

fn bump(n: &var i64) { *n += 1 }

fn main() -> i64 {
    let fixed = 1
    bump(&var fixed)
    0
}""", mode="diag", title="A `let` cannot be borrowed mutably"),

        H("Slices"),
        P("A slice is a pointer and a length. Arrays coerce to slices, so a "
          "function taking `[i64]` accepts an array of any size — that is how "
          "you write code that does not care how long its input is."),
        S("""import std::io

fn sum(values: [i64]) -> i64 {
    var total = 0
    for v in values { total += v }
    total
}

fn largest(values: [i64]) -> i64? {
    if values.$isEmpty() { return nil }
    var best = values[0]
    for v in values { if v > best { best = v } }
    best
}

fn main() -> i64 {
    let five: [5:i64] = [3, 1, 4, 1, 5]
    let three: [3:i64] = [10, 20, 30]
    io::println("sum of five:  " + sum(five).$str())
    io::println("sum of three: " + sum(three).$str())
    // A range narrows a slice further; both bounds are optional.
    io::println("middle:       " + sum(five[1..4]).$str())
    io::println("tail:         " + sum(five[2..]).$str())
    io::println("largest:      " + largest(five).or(0).$str())
    io::println("of nothing:   " + largest(five[0..0]).hasValue().$str())
    0
}""", mode="run", title="One function, arrays of every length"),

        H("Raw pointers"),
        P("A raw pointer is an address and nothing else: no length, no "
          "guarantee it points at anything. Creating one is fine; every "
          "*dereference* is an unsafe operation, so it has to happen inside a "
          "function marked `@unsafe` or an `unsafe { }` block."),
        S("""import std::io

@unsafe
fn writeThrough(target: *var i64, value: i64) {
    *target = value
}

@unsafe
fn readThrough(source: *i64) -> i64 {
    *source
}

fn main() -> i64 {
    var cell = 7
    // The address is taken safely; only the dereference is unsafe.
    unsafe {
        writeThrough(&var cell as *var i64, 99)
        io::println("read back: " + readThrough(&cell as *i64).$str())
    }
    io::println("cell is now " + cell.$str())
    0
}""", mode="run", title="Reading and writing through a raw pointer"),
        S("""fn peek(p: *i64) -> i64 {
    *p          // no @unsafe, no unsafe block
}""", mode="diag", title="Dereferencing outside an unsafe context"),
        P("A raw pointer can also be indexed. `p[n]` is the nth element from "
          "it — the same arithmetic C does, on the pointee's size, with no "
          "length to check against. It is the one indexing form in the "
          "language that is never bounds checked, which is why it needs an "
          "unsafe context, and `*var T` to be written through."),
        S("""import std::io
import std::mem

@safe("the block holds four i64 and no index below goes past four")
fn main() -> i64 {
    let block = mem::allocator.allocate(4 as usize * mem::size_of<i64>())
    let cells = unsafe { block as *var i64 }

    var i = 0
    while i < 4 {
        unsafe { cells[i] = (i + 1) * 10 }
        i += 1
    }
    var total = 0
    i = 0
    while i < 4 {
        total += unsafe { cells[i] }
        i += 1
    }
    io::println(total.$str())

    mem::allocator.deallocate(block)
    0
}""", mode="run", title="Indexing raw memory"),
        N("A store through a raw pointer is raw in the other sense too: no "
          "reference counting happens, and the slot is not assumed to hold "
          "anything already. `mem::retain` and `mem::release` are how a "
          "container built this way keeps its books — see "
          "`std::collections::vector`.",
          label="No counting", tone="warn"),
        P("A wrapper that has genuinely established the invariant says so with "
          "`@safe(\"reason\")`. The reason is not decoration — it is the record "
          "of *why* the unchecked operation inside is sound, and it appears in "
          "the diagnostic if someone later breaks the assumption."),
        S("""import std::io

@unsafe
fn incrementThrough(cell: *var i64) {
    *cell += 1
}

/// The pointer below cannot dangle, so the unchecked write cannot be wrong.
@safe("the pointer names a live local that outlives the call")
fn bumped(start: i64) -> i64 {
    var cell = start
    unsafe { incrementThrough(&var cell as *var i64) }
    cell
}

fn main() -> i64 {
    io::println(bumped(41).$str())
    0
}""", mode="run", title="Justifying an unsafe operation"),
        N("`&value as *T` is the only way to get a raw pointer. There is none "
          "for a class instance — that would let you sidestep the reference "
          "count.", label="Getting an address"),
    ],
    keywords=["pointer", "borrow", "reference", "slice", "raw", "address",
              "unsafe", "deref", "&var", "raw indexing", "allocator"]))


# ===========================================================================
# Safety
# ===========================================================================
SECTIONS.append(Sec(
    "safety", "guarantees", "Safety levels",
    "Rune is memory safe by default and lets you turn that off deliberately, "
    "per build or per function. Nothing is unchecked by accident.",
    [
        H("The three levels"),
        T(["`--safety`", "Bounds", "Nil", "Division by zero", "Leak report",
           "Strong cycles"],
          [["`full` *(default)*", "checked", "checked", "checked", "yes",
            "**refused**"],
           ["`minimal`", "—", "checked", "—", "yes", "warned"],
           ["`none`", "—", "—", "—", "—", "warned"]],
          caption="Set it per build with `runec --safety <level>` or per "
                  "package with `safety = \"...\"` under `[build]`."),
        P("At `full`, a failed check aborts with a diagnostic naming the file "
          "and line — not undefined behaviour, and not a silent wrong answer."),
        S("""import std::io

fn main() -> i64 {
    let values: [4:i64] = [1, 2, 3, 4]
    var index = 0
    // The compiler cannot see how far this goes, so the check stays in.
    while index < 6 {
        io::println(values[index].$str())
        index += 1
    }
    0
}""", mode="panic", title="A bounds check firing"),
        P("An index the compiler *can* see is out of range never gets that far "
          "— it is a compile error, at every safety level."),
        S("""fn main() -> i64 {
    let values: [3:i64] = [1, 2, 3]
    values[7]
}""", mode="diag", title="A statically known overrun"),

        H("Integer overflow"),
        P("`+`, `-`, `*` and unary `-` on a fixed-width integer can produce a "
          "result that does not fit. What happens then follows the build "
          "rather than the safety level: a **debug build** (`-O0`, which is "
          "what `rune build` produces) traps with a panic, the way a bounds "
          "check does, and a **release build** (`-O1` and above, `rune build "
          "--release`) wraps in two's complement. The mistake is caught while "
          "the program is being written and costs nothing once it is "
          "shipped. `--overflow-checks` and `--no-overflow-checks` pin it "
          "either way, `overflow-checks = true|false` under `[build]` does "
          "the same for a package, and `--safety none` never traps."),
        S("""import std::io

fn main() -> i64 {
    var count: i8 = 127
    count += 1
    io::println(count)
    0
}""", mode="panic", title="Overflow in a debug build"),
        P("When the program *means* it — a hash that is supposed to wrap, a "
          "counter that must never — the operation says so by name and gets "
          "that behaviour at every setting: `$wrappingAdd`, `$saturatingAdd` "
          "and `$checkedAdd`, with `Sub` and `Mul` beside each, and the same "
          "family as free functions in `std::math`."),
        S("""import std::io
import std::math

fn main() -> i64 {
    let x: i8 = 127
    io::println(x.$wrappingAdd(1))         // -128: two's complement
    io::println(x.$saturatingAdd(1))       // 127: clamped at the limit
    io::println(x.$checkedAdd(1) ?? -1)    // nil, so -1
    io::println(x.$checkedAdd(0) ?? -1)    // 127: it fitted

    // A hash is supposed to wrap, and says so.
    let basis: u64 = 0xcbf29ce484222325
    let prime: u64 = 0x100000001b3
    io::println(math::wrappingMul(basis, prime))
    0
}""", mode="run", title="Wrapping, saturating and checked, by name"),
        T(["Form", "On overflow"],
          [["`a + b`, `a - b`, `a * b`, `-a`", "traps at `-O0`, wraps at `-O1` and above"],
           ["`a.$wrappingAdd(b)` `$wrappingSub` `$wrappingMul`", "wraps"],
           ["`a.$saturatingAdd(b)` `$saturatingSub` `$saturatingMul`", "clamps to the type's limits"],
           ["`a.$checkedAdd(b)` `$checkedSub` `$checkedMul`", "`nil`; otherwise `Some(result)`"],
           ["`math::wrappingAdd(a, b)` and the rest", "the same three families as functions"]],
          caption="Both operands share one integer type."),
        N("`@Config(overflow_checks == \"on\")` tells a declaration which "
          "world it is in, for the rare case that wants to know.",
          label="Asking at compile time"),

        H("`@unsafe` and `unsafe { }`"),
        P("`@unsafe` on a function says its body may perform unchecked "
          "operations and that calling it is itself unchecked. `unsafe { }` "
          "opens the same window for a single block. Both are visible at the "
          "call site, which is the point: unsafety is never inherited "
          "silently."),
        S("""import std::io

@unsafe
fn reinterpret(bits: u64) -> f64 {
    // Only legal because the caller has been told this is unchecked.
    *(&bits as *u64 as *f64)
}

fn main() -> i64 {
    let asFloat = unsafe { reinterpret(4614256656552045848) }
    io::println(asFloat.$str())
    0
}""", mode="run", title="An unsafe function and its window"),
        S("""@unsafe
fn raw(p: *i64) -> i64 { *p }

fn caller(p: *i64) -> i64 {
    raw(p)          // calling it is itself an unsafe operation
}""", mode="diag", title="Unsafety does not leak into safe code"),

        H("`@safe(\"reason\")`"),
        S("""import std::io
import std::mem

// The pattern: an unchecked core, a checked edge, and a reason on the seam
// saying why the edge is enough.
@unsafe
fn sumUnchecked(cells: *var i64, count: i64) -> i64 {
    var total = 0
    var i = 0
    while i < count {
        total += cells[i]
        i += 1
    }
    total
}

@safe("the block is sized for `count` cells and every one is written below")
fn sumOfSquares(count: i64) -> i64 {
    if count <= 0 { return 0 }
    let block = mem::allocator.allocate(count as usize * mem::size_of<i64>())
    if mem::isNull(block) { return 0 }
    let cells = unsafe { block as *var i64 }
    var i = 0
    while i < count {
        unsafe { cells[i] = (i + 1) * (i + 1) }
        i += 1
    }
    let total = unsafe { sumUnchecked(cells, count) }
    mem::allocator.deallocate(block)
    total
}

fn main() -> i64 {
    io::println(sumOfSquares(4).$str())
    io::println(sumOfSquares(0).$str())
    0
}""", mode="run", title="An unchecked core behind a checked edge"),
        P("Some functions are safe *because of an argument you can make*, not "
          "because the compiler proved it. `@safe` records that argument. It "
          "permits the function to expose a checked interface over an unchecked "
          "implementation, and the string is kept with the declaration."),
        S("""import std::io

@unsafe
fn divideUnchecked(a: i64, b: i64) -> i64 { a / b }

@safe("the divisor is compared against zero on the line above")
fn divide(a: i64, b: i64) -> i64? {
    if b == 0 { return nil }
    unsafe { divideUnchecked(a, b) }
}

fn main() -> i64 {
    io::println(divide(84, 2).or(0).$str())
    io::println(divide(84, 0).hasValue().$str())
    0
}""", mode="run", title="A checked wrapper over an unchecked core"),
        N("`@safe` without a reason is accepted but warns. A justification "
          "nobody wrote down is a justification nobody can check.",
          label="Say why", tone="warn"),

        H("Borrows and ownership"),
        P("`full` also reads each function body for what it does with what it "
          "owns. This is a whole-body pass rather than a run-time check, so "
          "it costs nothing at run time and reports before the program is "
          "built. Below `full` each finding is a warning instead, and the "
          "build carries on."),
        T(["Reported", "Because"],
          [["A borrow returned from the function it points into",
            "the binding is destroyed on the way out, so the caller would be "
            "handed an address to nothing"],
           ["Two live borrows of one place, one of them able to write",
            "a writer has to be the only one"],
           ["A value that owns a resource read out of a field by value",
            "that would make a second owner, and the resource would be "
            "handed back twice"],
           ["A use of a binding that has been handed away",
            "it no longer refers to anything"]],
          caption="What the ownership pass reports at `--safety full`"),
        S("""fn dangling() -> &i64 {
    let n = 5
    &n
}

fn main() -> i64 { *dangling() }""",
          mode="diag", title="A borrow that outlives what it borrows"),
        S("""struct Point { var x: i64, var y: i64 }

fn main() -> i64 {
    var p = Point { x: 1, y: 2 }
    let a = &var p
    let b = &p
    a.x + b.y
}""", mode="diag", title="Two borrows, one of them mutable"),
        P("A borrow stops mattering after its last mention rather than at the "
          "end of the block, so finishing with one and then reading the value "
          "again is fine."),
        S("""import std::io

struct Point { var x: i64, var y: i64 }

fn main() -> i64 {
    var p = Point { x: 1, y: 2 }
    {
        var a = &var p
        a.x = 3
    }
    let b = &p              // the first borrow is finished with
    io::println(b.x)
    0
}""", mode="run", title="A borrow that has been finished with"),
        N("A borrow is followed back to the binding it starts from and the "
          "fields named on the way, so `&var p.x` and `&var p.y` are "
          "different places — the same rule the Zombie checker keeps, so a "
          "program it accepts is accepted here too. An index ends the path: "
          "`v[i]` stands for all of `v`, since following an index the compiler "
          "cannot evaluate would report conflicts with less certainty, not "
          "more.",
          label="How precise it is"),

        H("What it buys at run time"),
        P("The same pass answers a question nothing reports: which locals "
          "never leave the scope that declared them. A class built for one of "
          "those needs no reference counting at all — nothing else can hold "
          "it, so the allocation's own count *is* the binding's, and the "
          "scope hands it back on the way out. The retain, the temporary and "
          "the paired release all go."),
        SH("""$ runec --emit-llvm demo.rune -o demo.ll

  ; without the analysis
  %Point = call ptr @rune_alloc(i64 24, ptr @typeinfo)
  call void @Point_init(ptr %Point, i64 3)
  store ptr %Point, ptr %temp
  %0 = call ptr @rune_retain(ptr %Point)
  %1 = load ptr, ptr %p
  call void @rune_release(ptr %1)
  store ptr %Point, ptr %p
  %2 = load ptr, ptr %temp
  call void @rune_release(ptr %2)

  ; with it
  %Point = call ptr @rune_alloc(i64 24, ptr @typeinfo)
  call void @Point_init(ptr %Point, i64 3)
  store ptr %Point, ptr %p"""),
        P("Anything the pass cannot follow is treated as escaping: a value "
          "captured by a closure, cast to a raw pointer, stored in a field, "
          "or passed to a call by value. So the elision only ever applies "
          "where the whole story is visible in one body."),

        H("What is still your responsibility"),
        P("Memory safety here means no out-of-bounds access, no use of a "
          "released object, and no dereference of `nil` — inside safe code. It "
          "does not mean your program cannot deadlock, cannot exhaust memory, "
          "or cannot leak a reference cycle. Cycles are diagnosed at exit "
          "rather than prevented, and `weak` is the tool for them."),
        P("The borrow check is function-local. A borrow handed to a call is "
          "the callee's business for the length of the call, and a raw "
          "pointer is not followed at all — which is what `unsafe` means."),
    ],
    keywords=["safety", "unsafe", "safe", "bounds", "overflow", "nil check",
              "memory safety", "borrow checker", "ownership", "move",
              "escape analysis", "reference counting elision"]))

# ===========================================================================
# Single ownership: the Zombie borrow checker
# ===========================================================================
SECTIONS.append(Sec(
    "zombie", "guarantees", "Single ownership without a count",
    "Reference counting is the default, but it is not the only choice. Built "
    "with `--memory zombie`, a program keeps no counts at all: every value has "
    "exactly one owner, is handed on by moving, and is destroyed the moment "
    "its owner's scope ends. A second, precise borrow checker — Zombie — "
    "proves that every borrow is finished before the value it points at is "
    "gone, so nothing dangles and nothing is freed twice.",
    [
        H("Turning it on"),
        P("`--memory zombie` on the command line, or `memory = \"zombie\"` under "
          "`[build]` in `Rune.toml`, compiles the whole program — and its "
          "standard library — under single ownership. `--memory arc` (the "
          "default) is reference counting, unchanged. A library is tagged with "
          "the mode it was built for, and mixing the two in one program is a "
          "hard error, because the object code of each assumes its own "
          "convention."),
        SH("runec --memory zombie -o app app.rune\n"
           "rune build            # with memory = \"zombie\" in Rune.toml"),
        N("Zombie is a whole second memory model, not a stricter setting of the "
          "first. Its findings are always errors — at every `--safety` level "
          "— because the generated code has no counts to fall back on: the "
          "checker's verdict is what makes it sound.", label="A model, not a dial"),

        H("Values move; borrows look"),
        P("Anything the heap owns — a class, a `String`, a closure, a mark "
          "object — has one owner. Assigning it, passing it by value, "
          "returning it or capturing it by value all move it, and the binding "
          "it came from is empty afterwards. A borrow, `&x` or `&var x`, reaches "
          "the value without owning it, and takes nothing away."),
        S('''import std::io

class Greeting {
    text: String
    fn init(self, text: String) { self.text = text }
}

// `&self`: borrows the greeting, so the caller keeps it.
fn shout(g: &Greeting) -> String { g.text + "!" }

fn main() -> i64 {
    let hello = Greeting("hello")
    io::println(shout(&hello))      // borrowed: `hello` is still ours
    let moved = hello               // moved: `hello` is empty from here
    io::println(moved.text)
    0
}''', mode="run", memory="zombie", title="A value borrowed, then moved"),
        P("Use a value after it has been moved and the checker stops the build, "
          "naming where it went."),
        S('''import std::io

class Box { var v: i64  fn init(self, v: i64) { self.v = v } }
fn take(b: Box) -> i64 { b.v }

fn main() -> i64 {
    let x = Box(1)
    let a = take(x)         // `x` is moved into `take`
    let b = take(x)         // and cannot be used again
    io::println((a + b).$str())
    0
}''', mode="diag", memory="zombie", title="Use after move"),
        P("When a copy is what you meant, ask for one. `$clone()` builds a "
          "second value that owns everything the first did — a fresh "
          "`String`, a new object with each field cloned in turn."),
        S('''import std::io

class Box { var v: i64  fn init(self, v: i64) { self.v = v } }

fn main() -> i64 {
    let a = Box(7)
    let b = a.$clone()      // an independent copy
    b.v = 8
    io::println(a.v.$str() + " " + b.v.$str())   // 7 8
    0
}''', mode="run", memory="zombie", title="An explicit copy"),
        P("`$clone()` is what a type's own `clone` is reached through, "
          "wherever that method is written — in the body, in an `extend`, or "
          "supplied by a `bind` — and however deep in the value it sits: a "
          "struct holding a `Vector` is cloned by cloning the vector, which "
          "copies its storage rather than handing out a second holder of the "
          "same block."),
        S('''import std::io
import std::collections::vector

struct Tally { counts: vector::Vector<i64>, name: String }

fn main() -> i64 {
    var one = Tally { counts: vector::Vector<i64>(), name: "first" }
    one.counts.push(1)

    var two = one.$clone()      // the vector is copied, not shared
    two.counts.push(2)

    io::println(one.counts.length().$str() + " and " +
                two.counts.length().$str())
    0
}''', mode="run", title="A clone that reaches the parts"),
        P("What the compiler will **not** do is copy a value that owns "
          "something: a copy made field by field would hand one obligation to "
          "two values, and the second to go would close the same descriptor, "
          "or free the same block, a second time. Such a type says what a "
          "copy of it means by writing `clone` itself."),
        S('''struct Descriptor { @resource fd: i32 = -1 }

extend Descriptor {
    @safe("the descriptor is ours, and the flag stops a second close")
    fn deinit(&var self) {
        if self.fd >= 0 { self.fd = -1 }
    }
}

fn main() -> i64 {
    let one = Descriptor { fd: 7 }
    let two = one.$clone()
    0
}''', mode="diag", title="What the compiler will not copy"),
        N("`std::mem::Clone` is the mark that stands behind all of this, and "
          "it is automatic: a type has it when every part of it has it, and a "
          "type that owns something claims it — `bind mem::Clone to T {}` — "
          "once it has written `clone`. Write `T: mem::Clone` as a bound when "
          "a function of your own has to copy what it is given. See "
          "*Marks → Automatic marks*.", label="The mark behind `$clone`"),

        H("The checker is precise"),
        P("A borrow lasts until its last use, not to the end of the block, so a "
          "value can be borrowed, finished with, and then moved or changed. Two "
          "borrows of different fields never clash. And a method that takes "
          "`&var self` may still read the receiver while its own arguments are "
          "worked out — the receiver is reserved when the call is written "
          "and becomes exclusive only when it runs — so `self`-reading "
          "arguments to a mutating method are fine."),
        S('''import std::io

struct Point { var x: i64, var y: i64 }

class Counter {
    var n: i64
    fn init(self) { self.n = 0 }
    fn count(&self) -> i64 { self.n }
    fn add(&var self, by: i64) { self.n += by }
}

fn main() -> i64 {
    var p = Point { x: 1, y: 2 }
    let a = &var p.x
    let b = &var p.y        // a different field: no conflict
    *a += *b
    io::println(p.x.$str())              // 3

    var c = Counter()
    c.add(c.count() + 5)    // reads `self` for the argument, then mutates it
    io::println(c.count().$str())        // 5
    0
}''', mode="run", memory="zombie", title="Last-use, disjoint fields, and two-phase borrows"),
        P("Taking `&var` and `&` of the same value at once is refused, because a "
          "writer has to be the only one that can reach it."),
        S('''struct Point { var x: i64, var y: i64 }
fn main() -> i64 {
    var p = Point { x: 1, y: 2 }
    var a = &var p
    let b = &p              // a reader while `a` can still write
    a.x + b.y
}''', mode="diag", memory="zombie", title="Two borrows, one of them mutable"),

        H("Reference-counted code, ported"),
        P("Most reference-counted code compiles under `--memory zombie` "
          "unchanged: constructing values, calling methods, passing arguments, "
          "printing, building containers and looping over them all read exactly "
          "the same. What changes is underneath — a value that was shared is now "
          "moved — and the checker points at the one line where that matters "
          "rather than making you rewrite the rest."),
        P("Two habits answer almost everything it asks for. When you hand a "
          "value on but still need it, **borrow** it with `&` — most functions "
          "that only read already take `&T`, so a bare name auto-borrows and "
          "nothing changes at the call. When you need a second value that lives "
          "on its own, **copy** it with `$clone()` — a share under counting, an "
          "independent value under single ownership."),
        S('''import std::io
import std::collections::vector

class Account { var balance: i64  fn init(self) { self.balance = 0 } }

fn main() -> i64 {
    var names = vector::Vector<String>()
    names.push("ada")
    names.push("grace")
    for name in names { io::println(name) }        // borrows each; `names` stays whole
    io::println(names.length().$str())             // 2 — still ours

    let a = Account()
    let b = a.$clone()                             // a second, independent account
    b.balance = 100
    io::println(a.balance.$str() + " " + b.balance.$str())   // 0 100
    0
}''', mode="run", memory="zombie", title="The same code, either mode"),

        H("Where a reference is borrowed from"),
        P("A returned reference has to point somewhere that outlives the call. "
          "The checker works out where from on its own, from the body, so most "
          "functions say nothing. When you want the boundary written down — "
          "to pin a public interface, or where a result could come from more "
          "than one argument — a `from` clause names the place, as a plain "
          "path rather than an invented lifetime name."),
        S('''fn longest(a: &String, b: &String) -> &String from (a, b) {
    if a.$length() > b.$length() { a } else { b }
}''', mode="decls", memory="zombie", title="`from` names the places a result may borrow"),
        G("Type       ::= ... | RefType OriginClause?\n"
          "OriginClause ::= 'from' ( Place | '(' Place (',' Place)* ')' )\n"
          "Place      ::= ( 'self' | ident ) ( '.' ident )*   |   'global'"),
        P("Returning a borrow of a local is refused whatever the annotation: the "
          "local is gone the moment the function returns."),
        S('''fn dangling() -> &i64 {
    let n = 5
    &n
}
fn main() -> i64 { *dangling() }''', mode="diag", memory="zombie",
          title="A borrow that does not outlive the call"),
        P("A `from` clause is not only for results. On a **parameter** it is a "
          "requirement on the caller: the argument must borrow from the named "
          "place and nowhere else. This is what keeps a value handed to a thread "
          "off the caller's own locals — see `thread::scope` — and it is checked "
          "at the call, not in the body."),
        S('''class Item { pub v: i64  fn init(self, v: i64) { self.v = v } }
class List {
    head: Item
    fn init(self, h: Item) { self.head = h }
    fn first(&self) -> &Item from self { &self.head }
}
// `attach` promises the caller only ever hands it something borrowed from
// `list`, so what goes in cannot outlive the list it goes into.
fn attach(list: &var List, item: &Item from list) { }''', mode="decls", memory="zombie",
          title="`from` on a parameter: a caller-side contract"),
        P("On a **local binding** it is an assertion the checker verifies — for "
          "documentation, or to narrow what would otherwise be inferred:"),
        S('''class Item { pub v: i64  fn init(self, v: i64) { self.v = v } }
class List {
    head: Item
    fn init(self, h: Item) { self.head = h }
    fn first(&self) -> &Item from self { &self.head }
}
fn head(list: &List) -> &Item from list {
    let first: &Item from list = list.first()   // verified against `list`
    first
}''', mode="decls", memory="zombie", title="`from` on a local binding"),
        P("A **longer lifetime coerces to a shorter one**. A borrow of a `global` "
          "outlives every place a `from` clause could name, so it satisfies any "
          "of them: a function that promises `-> &String from a` may return a "
          "global, and an argument required `from list` may be one. The caller "
          "keeps the named place alive, which is more than a `'static` borrow "
          "ever needs — the place-based reading of `&'static T` fitting where "
          "`&'a T` was asked for. A string literal is the same case: it is "
          "interned once and never freed, so `&\"text\"` may be taken "
          "outright and goes wherever a borrow of a global goes."),
        S('''global BANNER: String = "welcome"
// Promises to borrow from `a`; returning the global is accepted, because a
// global outlives `a` — and so is a literal.
fn label(a: &String) -> &String from a {
    if a.$isEmpty() { &BANNER } else { a }
}
fn labelOr(a: &String) -> &String from a {
    if a.$isEmpty() { &"(none)" } else { a }
}''', mode="decls", memory="zombie", title="A longer lifetime coerces to a shorter"),
        N("The coercion is for what is immortal, not for what happens to hold "
          "it: `let h = \"Hello\"` is a local, which can be reassigned, so "
          "`&h` is a borrow of `h` and no more.",
          label="A local is still a local"),

        H("Views: which fields a method touches"),
        P("A `&var self` method that only touches some of the object's fields "
          "can say so with a view, `{ field, ... }` after the receiver. A caller "
          "may then hold a borrow of another field across the call. The checker "
          "infers a view for every method on its own; writing one pins it as "
          "part of the interface and lets a caller be checked without reading "
          "the body."),
        S('''import std::io

class Ledger {
    var total: i64
    var note: String
    fn init(self) { self.total = 0; self.note = "" }

    // Promises to touch `total` and nothing else.
    fn add(&var self { total }, n: i64) { self.total += n }

    fn label(&self) -> &String from self { &self.note }
}

fn main() -> i64 {
    var l = Ledger()
    let tag = l.label()     // a borrow of `note`
    l.add(10)               // touches only `total`: allowed alongside `tag`
    io::println(tag + " " + l.total.$str())
    0
}''', mode="run", memory="zombie", title="A view keeps a method out of the fields it does not name"),

        H("Internal references"),
        P("A field may borrow from another field of the same value, written "
          "`from self.field`. Such a struct owns everything it needs: it can be "
          "moved, returned and passed on, because moving it moves the handle to "
          "the borrowed data, not the data itself. The borrowed-from field has "
          "to own something on the heap — a `String`, a class — and be "
          "declared before the field that points into it."),
        S('''import std::io

struct Message {
    text: String
    body: &String from self.text     // points into this value's own `text`
}

fn parse(text: String) -> Message {
    let body = &text
    Message { text: text, body: body }
}

fn main() -> i64 {
    let m = parse("hello world")     // moved out of `parse`, borrow and all
    io::println(m.body.$length().$str())   // 11
    0
}''', mode="run", memory="zombie", title="A struct that borrows from itself"),

        H("The standard library runs on it"),
        P("The whole standard library compiles and runs under single ownership — "
          "`Vector`, `Map` and `Set`, `Option` and `Result`, `String` and the "
          "text routines, files and streams, JSON, the command-line parser. A "
          "container hands back a copy of what it is asked for rather than a "
          "shared reference (`v.at(0)` clones the element), and moves values in "
          "and out of its own storage with `std::mem`. How you call it does not "
          "change between the two modes: the same program builds either way, "
          "which is the whole point — anything that compiles under `zombie` "
          "compiles under `arc` too."),

        H("When a borrow has to wait for run time"),
        P("Sometimes two parts of a program genuinely reach one value and the "
          "compiler cannot see that only one touches it at a time. "
          "`mem::Checked<T>` moves the check to run time: `borrow()` hands out a "
          "read-only `Ref`, `borrowVar()` the one writable `RefVar`, and asking "
          "for a conflicting one aborts rather than letting them race. It keeps "
          "no count — one state word — so it reads the same in both modes, and "
          "it is what the exclusive-borrow diagnostics point to."),
        S('''import std::io
import std::mem

fn main() -> i64 {
    let cell = mem::Checked<i64>(0)
    { var w = cell.borrowVar(); *w = 41; *w = *w + 1 }   // exclusive, checked
    io::println((*cell.borrow()).$str())                  // 42
    0
}''', mode="run", memory="zombie", title="A borrow decided as the program runs"),

        H("Handles instead of back-references"),
        P("A `weak` back-reference needs a count to know when its target is "
          "gone, so single ownership does without it. `mem::Arena<T>` takes its "
          "place: it owns its entries and hands out small `Slot` handles that "
          "stay valid until an entry is removed. A handle to a removed slot is "
          "caught by a generation stamp rather than dangling — so a graph or a "
          "cache keeps arena handles where it would have kept weak pointers."),
        S('''import std::io
import std::mem

fn main() -> i64 {
    var a = mem::Arena<String>()
    let h = a.insert("first")
    a.remove(h)
    io::println(a.get(h).isNil().$str())    // true: the handle went stale
    0
}''', mode="run", memory="zombie", title="A generational handle"),

        H("Threads that borrow shared data"),
        P("`thread::scope` runs threads that may borrow the data around them and "
          "joins every one before it returns, so a borrow a thread takes never "
          "outlives what it points at. A thread is only ever handed something "
          "built from the scope's environment — `argument: A from self`, checked "
          "at the call — never a local of the body it could outlive. A shared "
          "`&T` may cross into a thread exactly when `T` is `Sync`."),
        S('''import std::io
import std::thread
import std::atomic

fn bump(c: &atomic::Counter) -> i64 {
    var i = 0
    while i < 100 { c.increment(); i += 1 }
    0
}

fn main() -> i64 {
    let counter = atomic::Counter(0)
    let total = thread::scope(counter,
        ||(s: &thread::Scope<atomic::Counter>) -> i64 {
            let a = s.spawn(bump, s.env())
            let b = s.spawn(bump, s.env())
            a.join()
            b.join()
            s.env().load()
        })
    io::println(total.$str())     // 200
    0
}''', mode="run", memory="zombie", title="Scoped threads over shared state"),

        H("What single ownership does without"),
        P("A `weak` field cannot tell when its target has been freed without a "
          "count, so it is an error; keep a `mem::Arena<T>` handle or an ordinary "
          "borrow instead. A library can also mark a type reference-counting-only "
          "with `@zombie_unavailable(\"…\")`, and reaching for it under `--memory "
          "zombie` fails with the alternative spelled out."),
        S('''class Parent { name: String  fn init(self, n: String) { self.name = n } }
class Child {
    weak owner: Parent?
    fn init(self) { self.owner = nil }
}
fn main() -> i64 { 0 }''', mode="diag", memory="zombie",
          title="`weak` needs a count"),
        N("Two escape hatches exist for the code the checker cannot vouch for. "
          "`@zombie(\"reason\")` on a function tells the checker to trust its "
          "body, the way `@safe` does for an unsafe call; its signature is still "
          "the contract callers are held to. And `unsafe { }` leaves raw "
          "pointers untracked, exactly as under reference counting.",
          label="When you know better"),

        H("The guards"),
        P("Every rule Zombie enforces has a code and a message that says what to "
          "do about it. They are errors at every `--safety` level, because the "
          "generated code keeps no count to fall back on — the checker's verdict "
          "is what makes it sound."),
        T(["Code", "The rule it enforces"],
          [["E0270", "a `&var` borrow is exclusive — no other borrow of an overlapping place may be live at once"],
           ["E0272", "a returned reference must outlive the call, so it may not borrow a local"],
           ["E0273", "a value cannot be used after it is moved"],
           ["E0274", "a value cannot be moved out of a borrow, raw memory, or a type with a `deinit`"],
           ["E0275", "a value cannot be moved while it is borrowed"],
           ["E0277", "a partly-moved value cannot be used whole"],
           ["E0278", "no write to a place while it is borrowed"],
           ["E0279", "no read of a place that is borrowed as `&var`"],
           ["E0280", "a borrow cannot outlive the value it points at"],
           ["E0281", "the body may borrow from no more than the result's `from` clause allows"],
           ["E0282", "a reference result the compiler cannot trace needs an explicit `from`"],
           ["E0283", "an argument must borrow from the place its parameter's `from` names"],
           ["E0286", "a method may touch only the fields its view names"],
           ["E0288", "`weak` needs a count, which single ownership does not keep"],
           ["E0289", "a global cannot be borrowed as `&var`"],
           ["E0290", "no write to a field through a shared `&self`"],
           ["E0292", "a reference-counting-only declaration is unavailable"],
           ["E0293", "a `from` place must name a parameter, `self`, or `global`"],
           ["E0294", "`@zombie` needs a reason"],
           ["E0296", "an internal reference must point into a heap-owned field"],
           ["E0297", "an internal reference must borrow from a field of its own value"],
           ["E0298", "a field that borrows another must be declared after it"],
           ["E0299", "a written view must cover everything the body touches"]],
          caption="The Zombie guards"),
        N("Findings inside the standard library are reported too, so a change "
          "that made a library body unsound is caught where it is written rather "
          "than miscompiling in silence. `--no-zombie-stdlib` silences them if "
          "you ever need it; the bodies are read for their summaries either way.",
          label="Standard library"),
    ],
    keywords=["zombie", "single ownership", "borrow checker", "move", "moved",
              "from", "lifetime", "view", "internal reference", "clone",
              "no reference counting", "memory zombie",
              "two-phase borrow", "dangling", "use after move",
              "checked", "refcell", "arena", "generational handle", "slot",
              "thread scope", "scoped threads", "send", "sync", "guards",
              "no-zombie-stdlib"]))



# ===========================================================================
# Modules and visibility
# ===========================================================================
SECTIONS.append(Sec(
    "modules", "structure", "Modules, imports and visibility",
    "One file is one module. There are no headers and no forward declarations: "
    "the compiler resolves a module's own names in any order, and `pub` decides "
    "what anyone else can see.",
    [
        H("Declaration order does not matter"),
        P("Sema resolves a module in four passes — collect names, resolve "
          "shapes, resolve signatures, then check bodies — so a function may "
          "call one declared below it, and two types may refer to each other."),
        S("""import std::io

fn main() -> i64 {
    io::println(describe(Point { x: 3, y: 4 }))
    0
}

// Declared after both of its uses, and after the type it takes.
fn describe(p: Point) -> String {
    "(" + p.x.$str() + ", " + p.y.$str() + ")"
}

struct Point { x: i64, y: i64 }""", mode="run", title="Used above, declared below"),

        H("`pub`"),
        S("""import std::io

// A module keeps its workings to itself and publishes an answer. Nothing
// without `pub` can be named from outside this file.
pub struct Account { pub owner: String, balance: i64 }

global var nextId: i64 = 1

fn nextAccountId() -> i64 {
    let id = nextId
    nextId += 1
    id
}

pub fn open(owner: String, deposit: i64) -> Account {
    let _ = nextAccountId()
    Account { owner: owner, balance: deposit }
}

pub fn balanceOf(a: Account) -> i64 { a.balance }

fn main() -> i64 {
    let a = open("ada", 250)
    io::println(a.owner + " has " + balanceOf(a).$str())
    0
}""", mode="run", title="Public surface, private workings"),
        P("Everything is private to its module unless marked `pub`. That "
          "applies to functions, types, fields, methods, globals and marks "
          "independently: a public struct with private fields is a perfectly "
          "ordinary thing to write."),
        S("""/// Public type, mixed fields.
pub struct Reading {
    pub label: String       // callers may read and write this
    raw: i64                // module-private: an implementation detail
}

pub fn reading(label: String, raw: i64) -> Reading {
    Reading { label: label, raw: raw }
}

global scaleFactor: i64 = 100   // private to this module

pub fn scaled(r: Reading) -> f64 {
    (r.raw as f64) / (scaleFactor as f64)
}""", mode="decls", title="Visibility is per declaration"),
        T(["Marked", "Visible to", "Notes"],
          [["nothing", "its own module", "the default"],
           ["`pub`", "any module that imports it", "recorded in the `.rul`"],
           ["`pub` field", "readers of the type", "the type must be `pub` too"],
           ["`pub` method", "callers of the type", ""],
           ["`pub mark`", "anyone, to bind", "requirements come with it"]]),

        H("Importing"),
        P("`import path::to::module` brings a module into scope under its last "
          "name; members are reached with `::`. `import x as y` renames it. The "
          "standard library is implicitly available but still needs importing "
          "by name."),
        S("""import std::io
import std::math as m

fn main() -> i64 {
    io::println(m::squareRoot(144.0).$str())
    io::println(m::PI.$str())
    0
}""", mode="run", title="Importing and renaming"),
        P("A handful of names need no import at all, because the language "
          "itself is defined in terms of them: `Option`, `Result`, and their "
          "variants `Some`, `None`, `Ok` and `Err`."),
        S("""fn parse(text: String) -> Result<i64, String> {
    // No import: the prelude puts these in every module's scope.
    match text.$toInt() {
        Some(n) => Ok(n)
        None => Err("not a number: " + text)
    }
}""", mode="decls", title="The prelude"),

        H("Namespaces, and names that collide"),
        P("A package's name is the root of its namespace. `src/lib.rune` or "
          "`src/main.rune` **is** the package module, and every other file "
          "under `src/` is a module beneath it: `src/io.rune` in a package "
          "called `app` is the module `app::io`. Nothing is flattened, so two "
          "packages can both have a `shapes.rune` without ever meeting."),
        P("That means a package may freely declare a module whose short name "
          "is already taken by the standard library. `app::io` and `std::io` "
          "are different modules with different full names, and both are "
          "usable in the same file:"),
        S('// A package named `app` with its own `src/io.rune`, alongside `std::io`.\n// Both are reachable at once, because a qualified path is never ambiguous.\n\nfn main() -> i64 {\n    std::io::println("from the standard library")\n    std::io::println(app::io::render("from this package"))\n    0\n}', mode="frag", title="Two `io` modules, one file"),
        P("Three rules decide what a name means, and they are worth knowing in "
          "this order:"),
        T(["Rule", "What it means"],
          [["A **fully qualified path always works**",
            "`std::io::println(...)` and `app::io::render(...)` name exactly "
            "one thing each, with no import at all. When in doubt, or when two "
            "short names would collide, write the path out"],
           ["An **import binds the last segment**",
            "`import std::io` puts `io` in scope; `import app::io` puts `io` "
            "in scope too. The name bound is the short one, not the path"],
           ["The **last import wins**",
            "importing both leaves `io` meaning whichever came second. It is "
            "not an error, and nothing warns — the earlier one is simply "
            "shadowed, and is still reachable by its full path"]]),
        N("Because the last import wins silently, a file that imports two "
          "modules with the same short name should qualify both rather than "
          "rely on order. Reordering imports would otherwise change what the "
          "code means.", label="Where this bites", tone="warn"),
        P("A submodule is a module in its own right, not an item inside its "
          "parent. `json::io` resolves even though `json` declares nothing "
          "called `io`, and it resolves the same way whether `json` is a "
          "package in this build or a `.rul` on the search path:"),
        SH("""import json          // the package module
import json::io      // a module beneath it — a separate import

json::io::readLines(path)      // or fully qualified, with neither import"""),
        N("A binary compiles under `<package>__bin_<file>` rather than under "
          "the package's own name, which is what lets `src/main.rune` say "
          "`import <package>` and reach the package's library instead of "
          "finding itself.", label="How a binary imports its own library"),

        H("Packages and `.rul` libraries"),
        P("A library compiles to a single `.rul`: an object file plus the "
          "interface metadata for everything it marked `pub`. That is what "
          "replaces a header — importing from a dependency reads the metadata "
          "out of the `.rul`, so there is nothing to keep in sync."),
        SH("""$ runec --emit-lib -o libstatistics.rul src/lib.rune
$ runec -o report src/main.rune -I . -l statistics"""),
        N("`rune build` does all of this for you, including building path "
          "dependencies first and passing their `link` entries down to whatever "
          "depends on them.", label="Usually you do not do this by hand"),

        H("What is in a `.rul`"),
        P("A small container, and no more than it has to be. Every integer is "
          "little-endian, every string is a `u32` length followed by that "
          "many bytes of UTF-8, and there is no alignment or padding "
          "anywhere."),
        G("""magic       8 bytes  "RUNELIB\\1"
version     u32      the container's own version; 3 today
memory      u32      0 = reference counting, 1 = Zombie
name        string   the library's module name

flagCount   u32      `@Config` names set when this was built
  flag      string
valueCount  u32      `@Config` keys that had values
  key       string
  value     string

unitCount   u32      one per module compiled into the library
  path      string   the dotted module path, e.g. "geometry::shapes"
  source    string   its public interface, as Rune source

objectLen   u64
object      bytes    a native object file for one target"""),
        P("Three things about it are worth knowing."),
        T(["", "Why"],
          [["The interface is **source**, not a symbol table",
            "an importer re-parses it, so it gets the declarations exactly as "
            "they were written — including generic bodies, which "
            "monomorphisation needs, and `pub macro` definitions, which "
            "expansion needs. Everything not `pub` is stripped on the way in"],
           ["The **conditions** travel with it",
            "the interface is source, so its `@Config` conditions are "
            "answered again on import — and have to be answered the way they "
            "were when the object code was made, not the way the importer's "
            "own build would answer them"],
           ["One target, one memory model",
            "the object code bakes in retains and releases, or their absence "
            "and the moved-in argument convention. Importing a library built "
            "the other way is refused rather than linked, and there is no fat "
            "`.rul`: cross-compiling means building the dependency for that "
            "target too"]]),
        N("The version is checked exactly rather than for a range. A mismatch "
          "says \"built by a different compiler version\" and stops, because "
          "the format is small enough that rebuilding is always the right "
          "answer.", label="No forward compatibility"),
        P("What a library may export is everything a module may declare: "
          "types, generic types, enums, classes and their subclasses, marks "
          "with associated types and defaults, binds (including operators and "
          "`into` conversions), `extend` blocks, functions, globals, aliases "
          "and `pub macro`s. And what an importer may do with them is "
          "everything it could do with its own: name them, construct them, "
          "match them, **extend** them with methods of its own, and **bind** "
          "its own marks to them."),
    ],
    keywords=["module", "import", "pub", "visibility", "package", "rul",
              "header", "prelude"]))


# ===========================================================================
# Decorators
# ===========================================================================
SECTIONS.append(Sec(
    "decorators", "annotation", "Decorators",
    "A decorator is a compiler instruction attached to a declaration, written "
    "`@name` or `@name(arguments)`. They never change what code means — only "
    "how it is compiled, checked, or exposed.",
    [
        G("""decorator  ::= "@" identifier [ "(" argument { "," argument } ")" ]
             // `@alias("...")` adds a name; `@intrinsic("...")` has no body
argument   ::= expression | identifier ":" expression"""),
        T(["Decorator", "Applies to", "Effect"],
          [["`@unsafe`", "function", "the body may perform unchecked operations; "
            "calling it is unsafe"],
           ["`@safe(\"reason\")`", "function", "a checked interface over an "
            "unchecked implementation; records why"],
           ["`@inline`", "function", "asks the optimiser to inline it"],
           ["`@noinline`", "function", "forbids inlining"],
           ["`@export(\"name\")`", "function", "gives it that exact symbol "
            "name, for C to call"],
           ["`@alias(\"name\")`", "any declaration", "another name for it; a "
            "string, so it can hold what an identifier cannot"],
           ["`@as(\"name\")`", "an `extern` declaration",
            "renames it for Rune's side only; the symbol stays what C exports"],
           ["`@resource`", "a field",
            "the type's `deinit` has to release it, or say so at `--safety full`"],
           ["`@intrinsic(\"name\")`", "function", "the compiler answers it "
            "directly; how `std::mem` gets `size_of`"],
           ["`@sync(\"reason\")`", "a class",
            "it synchronises its own access, so it may cross a thread"],
           ["`@Doc(\"...\")`", "any declaration",
            "prose the compiler keeps; what `rune doc` reads"],
           ["`@type(Executable | Library | …)`", "**a file**",
            "what that file produces"],
           ["`@link(\"m\")`", "**a file**", "a native library this file needs"],
           ["`@linkpath(\"/opt/lib\")`", "**a file**", "where to look for "
            "them"]],
          caption="Unknown decorators are diagnosed, not ignored — a typo in a "
                  "decorator name is a mistake worth hearing about."),

        H("Safety decorators"),
        S("""import std::io

@unsafe
fn trustMe(p: *i64) -> i64 { *p }

@safe("the pointer comes from `&var` on a live local, so it cannot dangle")
fn readLocal() -> i64 {
    var value = 41
    unsafe { trustMe(&var value as *i64) + 1 }
}

fn main() -> i64 {
    io::println(readLocal().$str())
    0
}""", mode="run", title="@unsafe and @safe"),

        H("`@alias` and `@as`"),
        P("`@alias` *adds* a name: the declaration answers to both. `@as` "
          "*replaces* one, and only inside an `extern` block — the C library "
          "goes on exporting the name that was written, and Rune sees the new "
          "one. That is what lets a program import C's `bind` and still "
          "declare a `bind` of its own."),
        S("""import std::io

extern "C" {
    @as("cAbs")
    fn abs(v: i32) -> i32        // links against C's `abs`
}

/// A Rune function that keeps the name C also uses.
fn abs(v: i64) -> i64 { if v < 0 { 0 - v } else { v } }

@safe("abs touches no memory")
fn main() -> i64 {
    io::println(abs(-7))         // this one
    io::println(cAbs(-9i32))     // C's
    0
}""", mode="run", title="Two functions called `abs`"),
        S("""@as("other")
fn ordinary() -> i64 { 1 }

fn main() -> i64 { ordinary() }""",
          mode="diag", title="`@as` outside an `extern` block"),

        H("Code generation decorators"),
        S("""import std::io

@inline
fn square(n: i64) -> i64 { n * n }

@noinline
fn shout(text: String) { io::println(text + "!") }

fn main() -> i64 {
    shout("area is " + square(7).$str())
    0
}""", mode="run", title="@inline and @noinline"),

        H("Decorators you write yourself"),
        P("Any name that is not one of the built-ins above is a decorator the "
          "program has to have declared: a function whose **last parameter is "
          "a function**. The earlier parameters are the decorator's own "
          "arguments, and the decorated function fills the last one."),
        S("""import std::io

// The decorator: two arguments of its own, then the function it decorates.
fn route(method: String, path: String, handler: @function() -> ()) {
    io::println("registering " + method + " " + path)
}

@route("GET", "/health")
fn health() { io::println("200 healthy") }

@route("POST", "/reload")
fn reload() { io::println("202 reloading") }

fn main() -> i64 {
    io::println("--- main ---")
    health()
    0
}""", mode="run", title="`@route(...)` is a call to `route(..., health)`"),
        P("Each decorator is called **once, before `main`**, after the globals "
          "exist and in the order the decorators were written. That is what "
          "makes a registry possible: whatever the decorators filled is ready "
          "by the time anything reads it."),
        S("""import std::io
import std::collections::vector

global var table: vector::Vector<String>? = nil

fn routes() -> vector::Vector<String> {
    if table is Some(t) { return t }
    let made = vector::Vector<String>()
    table = made
    made
}

fn route(path: String, handler: @function() -> ()) { routes().push(path) }

@route("/health")
fn health() { }

@route("/version")
fn version() { }

fn main() -> i64 {
    let table = routes()
    let count = table.length()
    io::println(count.$str() + " registered before main")
    var i = 0
    while i < count {
        io::println("  " + table.get(i))
        i += 1
    }
    0
}""", mode="run", title="A registry filled before `main`"),
        T(["Rule", "Why"],
          [["the last parameter is a function", "that is where the decorated "
            "function goes"],
           ["the shapes have to agree", "the decorator receives the function "
            "it is written on"],
           ["the decorator cannot be generic", "there is nothing to infer its "
            "arguments from before `main`"],
           ["free functions only", "a method carries `self`, and no instance "
            "exists yet"],
           ["a built-in name wins", "`@inline` and the rest are the "
            "compiler's, and cannot be redefined"]]),
        S("""@memoise
fn slow(n: i64) -> i64 { n * 2 }

fn main() -> i64 { slow(21) }""", mode="diag",
          title="An unknown decorator is an error, not a comment"),
        S("""fn dec(label: String, f: @function() -> ()) { }

@dec("x")
fn f(v: i64) -> i64 { v }

fn main() -> i64 { 0 }""", mode="diag",
          title="The decorated function has to be the shape asked for"),
        N("`@function(i64)` takes an `i64` and returns nothing, and "
          "`@function() -> i64` takes nothing and returns one: the parentheses "
          "always hold the parameters, and the arrow always introduces the "
          "result.",
          label="Reading a function type"),

        H("`@alias`: a second name"),
        P("`@alias(\"other\")` puts a declaration in scope under another name "
          "as well as its own, and more than one is allowed. The name is a "
          "string, so it can hold characters an identifier cannot — which is "
          "what lets an operator answer to its punctuation."),
        S("""import std::io

struct Bag { count: i64 }

extend Bag {
    @alias("size")
    @alias("howMany")
    pub fn length(&self) -> i64 { self.count }
}

@alias("makeBag")
fn bag(n: i64) -> Bag { Bag { count: n } }

fn main() -> i64 {
    let b = makeBag(3)
    io::println(b.length().$str() + " " + b.size().$str() + " " +
                b.howMany().$str())
    0
}""", mode="run", title="One declaration, several names"),
        N("An alias is a name, not a copy: it reaches the same declaration, so "
          "there is one body to maintain and one symbol in the object file. "
          "Taking a name something else already holds is an error.",
          label="Same thing, other name"),

        H("`@type`: what a file produces"),
        P("A file can say what it is meant to become. `@type` comes before "
          "every other directive, because what a file produces is what decides "
          "how the rest of them are used. A file with a `main` and no `@type` "
          "is an executable, which is what the compiler already assumed."),
        T(["Written", "Produces"],
          [["`@type(Executable)`", "a linked program"],
           ["`@type(Library)`", "a `.rul` — object code plus the interface"],
           ["`@type(Shared)`", "a native shared library — `.dylib`, `.so` "
            "or `.dll`, for anything that can load one"],
           ["`@type(Object)`", "a `.o` and nothing else"],
           ["`@type(Assembly)`", "target assembly"],
           ["`@type(LLVM)`", "textual LLVM IR"],
           ["`@type(Macros)`", "nothing on its own — the file holds "
            "procedural macros, built and run while the *program* compiles"]],
          caption="A flag on the command line still wins: a build script has "
                  "the last word over a file's preference."),
        S("""@type(Object)

// No entry point is emitted for an object, so nothing runs global
// initialisers for it — anything constant has to be a function.
pub fn checksum(bytes: [u8]) -> u64 {
    var sum = 0 as u64
    for b in bytes { sum = sum * 31 + (b as u64) }
    sum
}""", mode="frag", title="A file that compiles to an object"),
        S("""@link("m")
@type(Object)

fn f() -> i64 { 0 }""", mode="diag", title="`@type` comes first"),

        H("`@link` and `@linkpath`: what a file needs"),
        P("These two belong to the *file*, not to any declaration in it, so "
          "they go at the very top — before the imports. A file that calls "
          "into a native library says so beside the `extern` block that "
          "declares it, instead of leaving the caller to pass `-l` and hope."),
        S("""@link("m")
@linkpath("/usr/lib")

import std::io

extern "C" {
    fn cbrt(x: f64) -> f64
    fn hypot(a: f64, b: f64) -> f64
}

@safe("libm is linked by the directives at the top of this file")
fn main() -> i64 {
    io::println(unsafe { cbrt(27.0) }.$str())
    io::println(unsafe { hypot(3.0, 4.0) }.$str())
    0
}""", mode="run", title="A file that links libm"),
        P("Each takes one or more strings, and both may appear more than "
          "once. What they name is merged with anything `-l` and `-L` asked "
          "for, and repeated names are passed once."),
        S("""import std::io

@link("m")

fn main() -> i64 { 0 }""", mode="diag", title="Too late to be a file directive"),

        H("Exporting to C"),
        P("`@export` fixes the symbol name so a C caller can find it. Without "
          "it, a public function's symbol is qualified by its module."),
        S("""/// Callable from C as `rune_add`.
@export("rune_add")
pub fn add(a: i64, b: i64) -> i64 { a + b }""",
          mode="decls", title="A stable symbol name"),
        H("Spelling and placement"),
        S("""@notarealdecorator
fn f() -> i64 { 0 }""", mode="diag", title="An unrecognised decorator"),
        N("Decorators sit on their own line above the declaration, or inline "
          "before it — both parse. One per line reads better when there are "
          "several.", label="Placement"),
    ],
    keywords=["decorator", "attribute", "@inline", "@export", "@unsafe",
              "@safe", "annotation", "@alias", "alias", "@intrinsic",
              "intrinsic", "second name", "@link", "@linkpath", "linking",
              "native library", "@type", "output", "executable", "object",
              "user decorator", "registry", "@route"]))


# ===========================================================================
# Foreign function interface
# ===========================================================================
SECTIONS.append(Sec(
    "ffi", "interop", "Calling C",
    "An `extern \"C\"` block declares functions that exist somewhere else. "
    "There is no marshalling layer and no generated glue: Rune's scalar types "
    "are C's scalar types.",
    [
        H("Declaring foreign functions"),
        S("""@link("m")

import std::io

extern "C" {
    fn fmod(a: f64, b: f64) -> f64
    fn atan2(y: f64, x: f64) -> f64
    fn ldexp(value: f64, exponent: i32) -> f64
}

@safe("libm's contract for these is total over the values passed below")
fn main() -> i64 {
    io::println(unsafe { fmod(10.0, 3.0) }.$str())
    io::println(unsafe { ldexp(1.5, 3) }.$str())
    let quadrant = unsafe { atan2(1.0, 1.0) }
    io::println((quadrant > 0.78 && quadrant < 0.79).$str())
    0
}""", mode="run", title="Three from libm"),
        S("""import std::io

extern "C" {
    fn abs(value: i32) -> i32
    fn strlen(text: CString) -> u64
    // `...` marks a variadic C function.
    printf(content: CString, ...) -> i32
}

fn main() -> i64 {
    io::println(abs(-17).$str())
    io::println(strlen("hello").$str())
    printf("printf reached us: %d\\n", 42)
    0
}""", mode="run", title="Three C functions"),
        T(["Rune", "C", "Notes"],
          [["`i8` … `i64`", "`int8_t` … `int64_t`", "exact widths"],
           ["`u8` … `u64`", "`uint8_t` … `uint64_t`", ""],
           ["`isize` / `usize`", "`intptr_t` / `uintptr_t`", "pointer width"],
           ["`f32` / `f64`", "`float` / `double`", ""],
           ["`bool`", "`bool`", "one byte"],
           ["`CString`", "`const char *`", "borrowed, NUL terminated"],
           ["`*T` / `*var T`", "`const T *` / `T *`", "unchecked"],
           ["`@cfunction(A) -> B`", "`B (*)(A)`", "a bare function pointer"],
           ["`struct`", "same layout", "cross it **by pointer**; see below"],
           ["`String`", "—", "**not** a C type; use `.$cstr()`"],
           ["`@function(A) -> B`", "—", "a closure; use `@cfunction`"],
           ["`class`", "—", "never crosses the boundary"]]),

        H("Structs"),
        P("A Rune `struct` of scalars has the layout C gives the same "
          "declaration, so the two describe one piece of memory. Cross it by "
          "**pointer**: what the ABIs agree on is what a pointer to a struct "
          "means, not when a struct itself travels in a register. "
          "`&value as *T` is how a Rune value's address becomes a C pointer."),
        S('''import std::io

struct Point { x: f64, y: f64 }

extern "C" {
    fn memcpy(dst: *var Point, src: *Point, size: u64) -> *var Point
    fn memcmp(a: *Point, b: *Point, size: u64) -> i32
}

fn main() -> i64 {
    let origin = Point { x: 0.0, y: 0.0 }
    let corner = Point { x: 3.0, y: 4.0 }
    var copy = Point { x: 0.0, y: 0.0 }
    let size = 16 as u64          // two f64, as C lays them out

    unsafe { memcpy(&var copy as *var Point, &corner as *Point, size) }
    io::println("copied: " + copy.x.$str() + ", " + copy.y.$str())

    let same = unsafe { memcmp(&corner as *Point, &copy as *Point, size) } == 0
    let differs = unsafe { memcmp(&corner as *Point, &origin as *Point, size) } != 0
    io::println("same: " + same.$str() + ", differs: " + differs.$str())
    0
}''', mode="run", title="A struct, by pointer"),
        N("Windows x64 passes an aggregate in a register only at 1, 2, 4 or 8 "
          "bytes wide and passes anything else indirectly. Rather than hand C "
          "something it will misread, a `struct` parameter or result that "
          "cannot travel by value on the target is refused with an error "
          "saying so. Pointers behave identically everywhere, which is why "
          "they are the advice and not the workaround.",
          label="Why not by value", tone="warn"),

        H("Pointers and out-parameters"),
        P("A C function has one result, so a second comes back through a "
          "pointer the caller supplies. `&var x as *var T` turns a Rune "
          "variable's address into one; the cast is the step out of the "
          "checked world, which is what needs `unsafe`."),
        S('''import std::io

extern "C" {
    // frexp splits a double into a fraction and an exponent. The fraction is
    // returned; the exponent is written through the pointer.
    fn frexp(value: f64, exponent: *var i32) -> f64
    fn strtol(text: CString, end: *var u64, base: i32) -> i64
}

@safe("frexp is total over finite doubles")
fn main() -> i64 {
    var exponent: i32 = 0
    let fraction = frexp(12.0, &var exponent as *var i32)
    io::println("12.0 = " + fraction.$str() + " * 2^" + exponent.$str())

    var rest: u64 = 0
    let parsed = unsafe { strtol("2026 and more", &var rest as *var u64, 10) }
    io::println("parsed " + parsed.$str())
    0
}''', mode="run", title="Results written through pointers"),
        P("An array or slice hands C the two halves it expects — a pointer to "
          "the first element and a count: `&values[0] as *i64` with "
          "`values.$length()`."),

        H("Callbacks"),
        P("`@cfunction(...)` is a bare C function pointer: a code address and "
          "nothing else. A top-level `fn` has no captured state, so its "
          "address alone is one, and it converts wherever one is wanted — "
          "which is what lets C drive Rune code."),
        S('''import std::io

extern "C" {
    fn qsort(base: *var u8, count: u64, size: u64,
             compare: @cfunction(*u8, *u8) -> i32)
}

/// qsort hands the comparator two pointers into the array it is sorting.
fn ascending(a: *u8, b: *u8) -> i32 {
    let x = unsafe { (a as *i64)[0] }
    let y = unsafe { (b as *i64)[0] }
    if x < y { return -1 }
    if x > y { return 1 }
    0
}

fn main() -> i64 {
    var values: [5:i64] = [42, 7, 19, 3, 25]
    unsafe { qsort(&var values[0] as *var u8, 5 as u64, 8 as u64, ascending) }

    var out = ""
    for v in values { out += v.$str() + " " }
    io::println(out)
    0
}''', mode="run", title="C's qsort, sorting with a Rune comparator"),
        N("`@function` is the *closure* type: code plus a captured "
          "environment, two words wide. C has nowhere to put the second, so "
          "writing one in an `extern` signature is an error rather than a "
          "wrong answer at run time. A closure genuinely cannot be a C "
          "callback — pass a top-level `fn`, and give C any state it needs "
          "through the `void *` such APIs usually carry for the purpose.",
          label="`@function` is not `@cfunction`", tone="warn"),

        H("Strings across the boundary"),
        P("`CString` is a borrowed pointer to NUL-terminated bytes; a string "
          "literal already is one. A Rune `String` is a counted buffer, so hand "
          "C its `.$cstr()` view — valid for the duration of the call."),
        S("""import std::io

extern "C" {
    fn strlen(text: CString) -> u64
    fn atoi(text: CString) -> i32
}

fn main() -> i64 {
    let built = "12" + "34"
    io::println("length: " + strlen(built.$cstr()).$str())
    io::println("parsed: " + atoi(built.$cstr()).$str())
    0
}""", mode="run", title="Passing a String to C"),
        N("The pointer from `.$cstr()` belongs to the string. Do not store it — "
          "if the `String` is released, the bytes go with it.",
          label="Lifetime", tone="warn"),

        H("When C's name is one you want"),
        P("C has had the whole namespace for fifty years, so a good name is "
          "often already taken — `bind`, `connect`, `open`, `write`. `@as` "
          "renames the foreign declaration for Rune's side only: the symbol "
          "the linker resolves is still the one that was written, and the "
          "name is yours again."),
        S("""import std::io

extern "C" {
    // Links against `strlen`; this file calls it `cLength`.
    @as("cLength")
    fn strlen(text: CString) -> u64
}

/// Which frees `strlen` up to mean something in Rune terms.
fn strlen(text: String) -> i64 { text.$charCount() }

@safe("strlen reads up to the NUL the String guarantees")
fn main() -> i64 {
    io::println(strlen("héllo"))                  // characters
    io::println(unsafe { cLength("héllo".$cstr()) })  // bytes
    0
}""", mode="run", title="Importing a name you also want to declare"),
        N("`@as` applies to an `extern` declaration and nowhere else — on an "
          "ordinary function it would silently do nothing, which is worse "
          "than being told, so it is refused. `@alias` is the one that adds a "
          "second name to a declaration of your own."),

        H("Wrapping a descriptor"),
        P("A C API that hands out a descriptor hands out an obligation with "
          "it. A struct with a `@resource` field and a `deinit` is how that "
          "obligation is written down: the value closes itself, and the "
          "compiler will not let a second owner of it exist. This is exactly "
          "what `std::net` is built out of."),
        S("""import std::io

extern "C" {
    fn open(path: CString, flags: i32) -> i32
    fn close(fd: i32) -> i32
    fn read(fd: i32, buffer: *var u8, count: usize) -> i64
}

/// An open file descriptor, and the promise to close it.
pub struct Descriptor {
    @resource fd: i32 = -1
}

extend Descriptor {
    @safe("the descriptor is ours, and the flag stops a second close")
    fn deinit(&var self) {
        if self.fd >= 0 {
            close(self.fd)
            self.fd = -1
        }
    }

    pub fn isOpen(&self) -> bool { self.fd >= 0 }
}

@safe("open either returns a descriptor or -1, which is what is checked")
pub fn openRead(path: String) -> Descriptor? {
    let fd = unsafe { open(path.$cstr(), 0i32) }
    if fd < 0 { return nil }
    Descriptor { fd: fd }
}

fn main() -> i64 {
    match openRead("/etc/hosts") {
        Some(d) => io::println("opened: " + d.isOpen().$str()),
        None    => io::println("could not open it"),
    }
    // Nothing closes it explicitly; going out of scope does.
    0
}""", mode="run", title="A descriptor that closes itself"),

        H("Linking"),
        P("Declaring a foreign function does not find it. Point the linker at "
          "the library with `-l` on the command line, or `link = [...]` in the "
          "manifest, where dependents inherit it automatically."),
        SH("""$ runec -o plot src/main.rune -L /usr/local/lib -l m -l png"""),
        S("""[package]
name = "statistics"
version = "0.1.0"

[build]
# Dependents of this package inherit `-l m` without repeating it.
link = ["m"]
link-paths = ["/usr/local/lib"]""", mode="frag", title="The same, in Rune.toml"),

        H("Packaging a C half"),
        P("A package that ships C alongside its Rune lists it, and `rune` "
          "compiles it with the same toolchain the rest of the build uses. "
          "That is also what makes such a package cross-compile: the C goes "
          "wherever the Rune goes."),
        S('''[package]
name = "ffi"
version = "0.1.0"

[build]
c-sources = ["c/shim.c"]
c-flags = ["-Wall", "-Wextra"]
link = ["m"]                    # a system library, asked for by name''',
          mode="frag", title="Rune.toml for a package with a C shim"),
        P("`examples/project/ffi` is that package: a C shim, an `extern` "
          "block wrapping it, and a test for every shape that crosses. It is "
          "built and run for the host and cross-compiled to Windows, which is "
          "how the rules on this page are checked."),
        SH("""$ cd examples/project/ffi
$ rune test                       # host
$ rune test --target mingw        # built for Windows, run under wine"""),

        H("Being called from C"),
        P("Go the other way with `@export`, which fixes the symbol name. The "
          "signature has to stay inside the shared vocabulary above — no "
          "classes, no `String`."),
        S("""@export("stats_mean")
pub fn mean(values: [f64]) -> f64 {
    if values.$isEmpty() { return 0.0 }
    var total = 0.0
    for v in values { total += v }
    total / (values.$length() as f64)
}

@export("stats_scale")
pub fn scale(value: f64, factor: f64) -> f64 { value * factor }""",
          mode="decls", title="C-callable entry points"),
        P("A C program that calls those links the Rune object **and** the "
          "runtime, because the exported code is ordinary Rune code and may "
          "allocate, retain or release like any other."),
        SH("""$ runec -c -o stats.o src/lib.rune
$ cc host.c stats.o -lruneruntime -lm -o host"""),
        N("`@export` gives a symbol strong linkage, so two of the same name "
          "collide rather than silently merging — which is what you want from "
          "something whose whole purpose is to answer to one exact name.",
          label="Exported names are unique"),
        P("`--shared` produces a loadable library instead of an object: a "
          "`.dylib`, a `.so` or a `.dll`, with the runtime already inside it. "
          "That is the form anything which loads code at run time wants — "
          "`dlopen`, Python's `ctypes`, a plugin host — and it needs no link "
          "line of its own."),
        SH("""$ runec --shared -o libstats.dylib src/lib.rune
$ python3 -c 'import ctypes; print(ctypes.CDLL("./libstats.dylib").stats_scale)'"""),
        P("What the library answers to is exactly what `@export` named. "
          "Everything else keeps its module-qualified symbol, which is the "
          "point: a shared library's surface is the list of `@export`s, "
          "written down in one place."),
        N("`@type(Shared)` says the same thing inside the file, for a source "
          "tree where the answer belongs with the code rather than in a build "
          "script. A flag on the command line still wins.",
          label="Or say it in the file"),

        H("C++ is its own block"),
        P("`extern \"C++\"` declares a C++ library directly — namespaces, "
          "classes, constructors, templates and all — with the compiler "
          "spelling each symbol the way the Itanium ABI spells it and passing "
          "each argument the way that target's C++ ABI passes it. See "
          "*Calling C++*."),
    ],
    keywords=["ffi", "extern", "c", "interop", "cstring", "link", "export",
              "variadic"]))


# ===========================================================================
# Calling C++
# ===========================================================================
SECTIONS.append(Sec(
    "cxx", "interop", "Calling C++",
    "An `extern \"C++\"` block declares what a C++ library exports. The "
    "compiler then does what a C++ compiler does at the call: spells the "
    "symbol the way the Itanium ABI spells it, and passes each argument the "
    "way that target's C++ ABI passes it. No `extern \"C\"` shim, no "
    "generated bindings.",
    [
        P("The difference from `extern \"C\"` is not the syntax but what the "
          "compiler has to know. A C symbol is its own name; a C++ symbol "
          "folds in the namespace, the class, the const-ness of the member "
          "and every parameter type. A C struct crosses by pointer because "
          "the ABIs agree about pointers; a C++ struct crosses **by value** "
          "here, because the compiler knows how this target passes that "
          "particular struct — in two registers, packed into one, or through "
          "memory with the result written back through a hidden pointer."),
        S('''extern "C++" {
    namespace geometry {
        struct Vec2 { x: f64, y: f64 }

        fn lengthSq(v: Vec2) -> f64
        fn scaled(v: Vec2, k: f64) -> Vec2
    }
}''', mode="decls", title="A namespace, a struct and two functions"),
        P("`namespace` nests as it does in C++ and affects the symbols only: "
          "the names it holds arrive in the module that wrote the block, so "
          "`lengthSq` above is called `lengthSq`, not `geometry::lengthSq`."),
        N("The mangling this compiler speaks is the Itanium C++ ABI, which is "
          "what Clang and GCC use on Linux, macOS, the BSDs and MinGW. A "
          "`-windows-msvc` target uses a different scheme, and an "
          "`extern \"C++\"` block for one is refused rather than "
          "mis-mangled.", label="Which C++ ABI", tone="warn"),

        H("C++'s own scalar names"),
        P("`long` is 64 bits on Linux and 32 on Windows, and `int64_t` is "
          "`long` on one and `long long` on the other — a distinction Rune's "
          "`i64` cannot make, and one the symbol depends on. So the C++ "
          "spellings exist as names of their own. Each is the Rune type it is "
          "on the target being built for, and in an `extern \"C++\"` "
          "signature it also fixes how the parameter mangles."),
        T(["Written", "C++", "Is, on a 64-bit Linux target"],
          [["`c_char`", "`char`", "`i8` — `u8` where `char` is unsigned"],
           ["`c_schar` / `c_uchar`", "`signed char` / `unsigned char`", "`i8` / `u8`"],
           ["`c_short` / `c_ushort`", "`short` / `unsigned short`", "`i16` / `u16`"],
           ["`c_int` / `c_uint`", "`int` / `unsigned`", "`i32` / `u32`"],
           ["`c_long` / `c_ulong`", "`long` / `unsigned long`", "`i64` / `u64`"],
           ["`c_longlong` / `c_ulonglong`", "`long long` / `unsigned long long`", "`i64` / `u64`"],
           ["`c_float` / `c_double`", "`float` / `double`", "`f32` / `f64`"],
           ["`c_bool`", "`bool`", "`bool`"],
           ["`c_size_t` / `c_ssize_t`", "`size_t` / `ssize_t`", "`u64` / `i64`"],
           ["`c_ptrdiff_t`", "`ptrdiff_t`", "`i64`"],
           ["`c_intptr_t` / `c_uintptr_t`", "`intptr_t` / `uintptr_t`", "`i64` / `u64`"],
           ["`c_int8_t` … `c_int64_t`", "`int8_t` … `int64_t`", "`i8` … `i64`"],
           ["`c_uint8_t` … `c_uint64_t`", "`uint8_t` … `uint64_t`", "`u8` … `u64`"],
           ["`c_wchar_t`", "`wchar_t`", "`i32` — `u16` on Windows"],
           ["`c_void`", "`void`", "`u8`; only useful behind a pointer"]],
          caption="Rune's own `i8`…`i64`, `u8`…`u64`, `f32`, `f64` and `bool` "
                  "are accepted too, and mangle as the target's `intN_t` "
                  "family — which is what a header that uses those means."),

        H("What crosses"),
        T(["Rune", "C++", "Notes"],
          [["`i8` … `i64`, `u8` … `u64`", "`int8_t` … `uint64_t`", "or the `c_` names above"],
           ["`f32` / `f64`", "`float` / `double`", ""],
           ["`bool`", "`bool`", ""],
           ["`CString`", "`const char *`", "borrowed, NUL terminated"],
           ["`*T` / `*var T`", "`const T *` / `T *`", "unchecked"],
           ["`&T` / `&var T`", "`const T &` / `T &`", "a reference **is** a pointer"],
           ["`struct` in the block", "the same struct", "**by value**, by the target's rules"],
           ["`struct<T>` in the block", "a class template", "one symbol per instantiation"],
           ["`enum` in the block", "the same enum", "an `int`"],
           ["`class` in the block", "the class", "only ever behind a pointer"],
           ["`@cfunction(A) -> B`", "`B (*)(A)`", "a bare function pointer"],
           ["`String`", "—", "not a C++ type; use `.$cstr()`"],
           ["`@function(A) -> B`", "—", "a closure; use `@cfunction`"],
           ["a Rune `struct` or `class`", "—", "declare the C++ one in the block"]]),

        H("Structs, by value"),
        P("A `struct` written inside the block is a C++ struct: Rune lays it "
          "out identically and hands it over the way C++ would. Three shapes "
          "that are passed three different ways on one machine, and "
          "differently again on the next, are written the same here."),
        S('''extern "C++" {
    namespace shim {
        struct Pair { a: c_int, b: c_int }              // one register
        struct Vec2 { x: f64, y: f64 }                  // two, or an HFA
        struct Wide { a: c_long, b: c_long, c: c_long } // through memory

        fn swapped(p: Pair) -> Pair
        fn scaled(v: Vec2, k: f64) -> Vec2
        fn tripled(w: Wide) -> Wide
    }
}''', mode="decls", title="Three shapes, one spelling"),
        N("This is the opposite of the advice for C, where a struct crosses "
          "by pointer because nothing tells the compiler which convention the "
          "other side used. Inside an `extern \"C++\"` block there is no such "
          "doubt: the C++ ABI for the target is what both sides follow.",
          label="Why by value here"),

        H("References and out-parameters"),
        P("A C++ reference is a pointer that is not written with a star. "
          "`&T` is `const T &` and `&var T` is `T &`, and a raw `*T` or "
          "`*var T` is accepted where one is wanted — it is the same address "
          "either way."),
        S('''extern "C++" {
    namespace shim {
        struct Pair { a: c_int, b: c_int }
        fn sumRef(p: &Pair) -> c_int      // int sumRef(const Pair &)
        fn bump(x: &var c_int, by: c_int) // void bump(int &, int)
    }
}

/// The borrows live for the call, which is all a reference needs.
@safe("both borrows name locals that outlive the call")
pub fn bumped(start: i64, by: i64) -> i64 {
    var x = start as c_int
    bump(&var x, by as c_int)
    x as i64
}''', mode="frag", title="Reference parameters, kept inside a wrapper"),

        H("Classes"),
        P("A C++ `class` is **opaque**: Rune never holds one by value, "
          "copies one, or destroys one, because only C++ knows how. It exists "
          "behind a pointer, and its members are reached through that. How "
          "the members are written says what they are:"),
        T(["Written", "Is"],
          [["`fn init(&var self, ...)`", "a constructor"],
           ["`fn deinit(&var self)`", "the destructor"],
           ["`fn name(&self) -> T`", "a `const` member function"],
           ["`fn name(&var self) -> T`", "a non-const member function"],
           ["`fn name(args) -> T`", "a `static` member function"],
           ["`@operator(\"[]\") fn at(&self, ...)`", "`operator[]`"],
           ["`class D : B`", "single, non-virtual inheritance"],
           ["`@size(N)`", "`sizeof` on the C++ side; what `cxx::alloc` needs"]]),
        S('''extern "C++" {
    namespace shim {
        struct Pair { a: c_int, b: c_int }

        @size(8)
        class Counter {
            fn init(&var self, start: c_int)
            fn deinit(&var self)
            fn next(&var self) -> c_int
            fn peek(&self) -> c_int
            fn setStep(&var self, step: c_int)
            fn state(&self) -> Pair
            fn make(start: c_int) -> *var Counter
            fn destroy(c: *var Counter)
        }

        /// `this` is one address for both halves, so the base's members are
        /// reached through a pointer to the derived class unchanged.
        @size(8)
        class Stepper : Counter {
            fn init(&var self, start: c_int, step: c_int)
            fn twice(&var self) -> c_int
        }
    }
}''', mode="decls", title="A class, its destructor, and a class derived from it"),

        H("Making one: `std::cxx`"),
        P("A C++ object lives where C++ can destroy it, so its storage comes "
          "from `operator new` and goes back to `operator delete` — never "
          "from Rune's allocator. `cxx::alloc<T>()` is the first half and "
          "`cxx::free` the second, with the constructor and destructor "
          "written out between them, exactly as placement new and an explicit "
          "destructor call are in C++."),
        S('''import std::cxx

@safe("the storage is ours from `alloc` until `free`, and nothing else holds it")
fn counting() -> i64 {
    let c = cxx::alloc<Counter>()   // @size(8) bytes from `operator new`
    c.init(10)                      // Counter::Counter(10), on that storage
    c.setStep(5)
    let first = c.next()
    c.deinit()                      // ~Counter()
    cxx::free(c)                    // back to `operator delete`
    first as i64
}''', mode="frag", title="The two halves of an object's life"),
        N("A library that hands objects out and takes them back — "
          "`Counter::make` and `Counter::destroy` above — is simpler still: "
          "call those and let it do both halves. `cxx::alloc` is for the "
          "classes that have no such pair.", label="Or let C++ do it"),
        T(["`std::cxx`", "Is"],
          [["`alloc<T>() -> *var T`", "`operator new(sizeof(T))`, uninitialised"],
           ["`free<T>(p: *var T)`", "`operator delete(p)`"],
           ["`null<T>() -> *var T`", "`nullptr`"],
           ["`isNull<T>(p) -> bool`", "`p == nullptr`"]]),

        H("Templates"),
        P("A generic `struct` in the block is a class template. Each "
          "instantiation is a different C++ type and gets the symbol that "
          "type gives it, so one declaration serves every element type the "
          "library was compiled for."),
        S('''extern "C++" {
    namespace shim {
        struct Span<T> { data: *T, len: c_ulong }

        fn total(s: Span<c_long>) -> c_long     // shim::total(shim::Span<long>)
        fn totalD(s: Span<f64>) -> f64          // shim::totalD(shim::Span<double>)
    }
}

/// A slice's two halves are exactly what a span holds.
@safe("the pointer and the count come from one slice, so the extent is right")
pub fn totalOf(values: [c_long]) -> i64 {
    if values.$isEmpty() { return 0 }
    total(Span<c_long> { data: &values[0] as *c_long,
                         len: values.$length() as c_ulong }) as i64
}''', mode="frag", title="A span, twice over"),

        H("Enums and variables"),
        S('''extern "C++" {
    namespace shim {
        enum Colour { Red = 1, Green = 2, Blue = 4 }
        fn brighter(c: Colour) -> Colour
        /// A namespaced variable has a mangled symbol too.
        var liveCounters: c_int
    }
}''', mode="decls", title="An enum and a variable"),

        H("Renaming, and operators"),
        P("`@as` renames a declaration for Rune's side only, exactly as it "
          "does in an `extern \"C\"` block — which is also how two overloads "
          "of one C++ name are told apart, since Rune has one name per "
          "declaration. `@operator` says which C++ operator a member is."),
        S('''extern "C++" {
    namespace llvm {
        class Type {}
        class FunctionType : Type {
            // One of the overloads of `FunctionType::get`, under a Rune name
            // of its own.
            @as("functionType")
            fn get(result: *var Type, isVarArg: bool) -> *var FunctionType
        }
        @size(8)
        class Counter {
            @operator("[]")
            fn at(&self, i: c_int) -> c_int
            @operator("new")
            fn allocate(n: c_size_t, tag: c_int) -> *var c_void
        }
    }
}''', mode="decls", title="`@as` and `@operator`"),
        P("`@operator` takes the operator as C++ writes it after the "
          "keyword: `\"new\"`, `\"delete\"`, `\"[]\"`, `\"()\"`, `\"+\"`, "
          "`\"==\"`, `\"<=>\"` and the rest."),

        H("Linking"),
        P("A file with an `extern \"C++\"` block links the target's C++ "
          "runtime automatically — libc++ where Apple ships it, libstdc++ "
          "where GCC does, and statically on MinGW so the executable carries "
          "no `libstdc++-6.dll`. Pass `--link-cxx` by hand for a program "
          "whose C++ only arrives through objects it links."),
        SH('''$ runec -o demo src/main.rune -L /usr/local/lib -l mylib'''),
        P("A package that ships C++ alongside its Rune lists it, and `rune` "
          "compiles it with the C++ driver that goes with the build's `cc` — "
          "so a package with a C++ half cross-compiles like any other."),
        S('''[package]
name = "ffi"
version = "0.1.0"

[build]
cxx-sources = ["cxx/shim.cpp"]
cxx-flags = ["-Wall", "-Wextra", "-fno-exceptions", "-fno-rtti"]
cxx-standard = "c++17"          # the default

[target.mingw]
triple = "x86_64-w64-mingw32"
cc = "x86_64-w64-mingw32-gcc"
cxx = "x86_64-w64-mingw32-g++"  # derived from `cc` when not given
runner = "wine"''', mode="frag", title="Rune.toml for a package with a C++ half"),
        P("`examples/project/ffi` is that package: a C shim, a C++ shim, both "
          "declared and both tested, built for the host and cross-compiled to "
          "Windows. `tests/cases/95_cxx_llvm.rune` goes further and drives "
          "LLVM's own C++ API — a context, a module, a function, an `add` and "
          "a `ret`, then the verifier — with no wrapper of any kind."),
        SH('''$ cd examples/project/ffi
$ rune test                       # host
$ rune test --target mingw        # built for Windows, run under wine'''),

        H("What does not cross"),
        T(["Not supported", "Instead"],
          [["virtual dispatch", "declare the member and call it on the exact "
            "type, or wrap the call in C++"],
           ["exceptions", "a library that throws must not throw across the "
            "boundary; build it `-fno-exceptions`, or catch inside"],
           ["`std::string`, `std::vector` and friends", "pass their "
            "`data()` and `size()`, or wrap in C++"],
           ["multiple or virtual inheritance", "single, non-virtual bases "
            "only — `this` has to be one address"],
           ["MSVC targets", "the Itanium ABI only: Clang, GCC, MinGW"],
           ["a Rune `String`, closure or class", "`CString`, `@cfunction`, or "
            "a C++ type declared in the block"]]),
        N("A member declared here is called **non-virtually**, by its own "
          "symbol. For a `virtual` function that is only right when the "
          "object's dynamic type is the one declaring it — which it is for a "
          "class you construct yourself, and is not for one handed to you "
          "through a base pointer. When in doubt, put a small non-virtual "
          "function in the C++ half and declare that.",
          label="Virtual functions", tone="warn"),
    ],
    keywords=["c++", "cxx", "extern c++", "interop", "itanium", "mangling",
              "namespace", "class", "constructor", "destructor", "template",
              "llvm", "operator new", "cxx-sources", "link-cxx"]))


# ===========================================================================
# Standard library
# ===========================================================================
SECTIONS.append(Sec(
    "stdlib", "library", "The standard library",
    "Small on purpose, and written in Rune over a C runtime you can read in an "
    "afternoon. `io`, `option`, `result`, `math` and `process` are what most "
    "programs touch; `mem` and `collections` are there for code that has to "
    "manage its own storage, `iter` is what `for` dispatches through, `any` "
    "is what a value of unknown type is asked about, `reflect` is what the "
    "compiler is asked about a type, `thread` is how a program does more "
    "than one thing at once and `task` how one thread keeps several things "
    "in progress, `net` is TCP in the shape `io`'s stream marks "
    "already describe, `fmt` is what a format "
    "string expands into, and `testing` is what a "
    "file under `tests/` reports through. `env`, `random`, `hash`, `json` "
    "and `cli` are the everyday things a program wants from outside "
    "itself — its environment, a number nobody can predict, a checksum, a "
    "document, its command line — and `time` keeps a calendar as well as a "
    "clock.",
    [
        H("std::thread"),
        T(["Name", "Signature", "Does"],
          [["`Send`", "`mark`", "may a value of this type move to another "
            "thread? Not declared — read off the type"],
           ["`Sync`", "`mark`", "may one be reached from several at once?"],
           ["`spawn`", "`<A: Send, R: Send>(entry: @cfunction(A) -> R, "
            "argument: A) -> Handle<R>`", "runs `entry(argument)` on a thread"],
           ["`Handle::join`", "`(&var self) -> R`",
            "waits, and hands back what the thread returned"],
           ["`Mutex<T>`", "`class`", "shared mutable state, reachable only "
            "while locked"],
           ["`Mutex::withLock`", "`(&var self, body: @cfunction(T) -> T)`",
            "replaces the value with `body(value)`, locked"],
           ["`Mutex::get` / `set`", "`(&self) -> T` / `(&var self, next: T)`",
            "read and write, locked"],
           ["`Arc<T: Sync>`", "`class`", "one value several threads may read"],
           ["`Arc::get`", "`(&self) -> T`", "a copy of what is inside"],
           ["`Channel<T: Send>`", "`class`",
            "a queue between threads; `send`, `receive`, `tryReceive`, "
            "`close`, `pending`"],
           ["`sleep`", "`(duration: time::Time)`",
            "stops this thread for at least that long"],
           ["`hardwareThreads`", "`() -> i64`",
            "how many run at once; never zero"],
           ["`yieldNow`", "`()`", "offer the rest of this turn"]],
          caption="See **Threads and sharing**. Nothing crosses a thread "
                  "boundary that the compiler cannot vouch for."),

        H("std::task"),
        T(["Name", "Signature", "Does"],
          [["`Future<T>`", "`class`", "work that will produce a `T`; what "
            "calling an `async fn` hands back"],
           ["`Future::await`", "`(&self) -> T`", "the result, parking the "
            "task until it is there — written `f.await`"],
           ["`Future::wait`", "`(&self) -> T`", "the same from code that is "
            "not `async`: blocks the thread, running the other tasks"],
           ["`Future::isDone`", "`(&self) -> bool`", "whether the result is "
            "there"],
           ["`Future::complete`", "`(&var self, value: T)`",
            "hands a `pending` future its value"],
           ["`spawn`", "`<T>(body: @function() -> T) -> Future<T>`",
            "starts `body` as a task; what `async { }` is"],
           ["`sleep`", "`(duration: time::Time) -> Future<()>`",
            "done once the time has passed"],
           ["`pending`", "`<T>() -> Future<T>`",
            "a future somebody will `complete`"],
           ["`blocking`", "`<A: Send, R: Send>(entry: @cfunction(A) -> R, "
            "argument: A) -> Future<R>`",
            "runs `entry` on a thread of its own"],
           ["`all`", "`<T>(futures: Vector<Future<T>>) -> Future<Vector<T>>`",
            "every result, in order"],
           ["`first`", "`<T>(futures: Vector<Future<T>>) -> Future<(i64, Future<T>)>`",
            "the first to finish: its index, and the future itself"],
           ["`race`", "`<T>(futures: Vector<Future<T>>) -> Future<T>`",
            "the value of the first to finish; the rest are cancelled"],
           ["`timeout`", "`<T>(limit: time::Time, future: Future<T>) -> Future<T?>`",
            "the value within `limit`, or `nil` and the task cancelled"],
           ["`Future::cancel`", "`(&self)`", "asks the task to stop at its "
            "next suspension point"],
           ["`Future::outcome`", "`(&self) -> Outcome<T>`",
            "waits; `Done(value)` or `Cancelled`"],
           ["`Future::isCancelled`", "`(&self) -> bool`",
            "whether a cancel has been asked for"],
           ["`Outcome<T>`", "`enum`", "`Done(T)` or `Cancelled`"],
           ["`checkpoint`", "`()`", "leaves the task here if cancelled"],
           ["`offload`", "`<R: Send>(body: @function() -> R) -> Future<R>`",
            "a closure on a worker thread; captures must be `Send`"],
           ["`offloadAsync`", "`<R: Send>(body: @function() -> Future<R>) -> Future<R>`",
            "an `async` closure on a worker thread's executor"],
           ["`workers`", "`() -> i64`", "how many worker threads the pool "
            "may run"],
           ["`readable` / `writable`", "`(descriptor: i64) -> Future<()>`",
            "done when the socket is ready"],
           ["`Future::clone`", "`(&self) -> Self`",
            "another handle to the same task; what `$clone()` does"],
           ["`run`", "`<T>(future: Future<T>) -> T`",
            "`wait()`, spelled for a `main`"],
           ["`yieldNow`", "`()`", "let every ready task run first"],
           ["`inTask`", "`() -> bool`", "inside a task, or on the thread's "
            "own stack"],
           ["`stackSize` / `setStackSize`", "`() -> i64` / `(bytes: i64)`",
            "the stack each new task is given"]],
          caption="See **Tasks and futures**. Tasks on one thread take "
                  "turns, so nothing here asks for `Send` — except "
                  "`blocking`, which is a thread."),

        H("std::atomic"),
        T(["Name", "Signature", "Does"],
          [["`Counter`", "`class`", "a number several threads may change"],
           ["`Counter::load` / `store`", "`(&self) -> i64` / `(&var self, i64)`",
            "read, write"],
           ["`Counter::add` / `sub`", "`(&var self, delta: i64) -> i64`",
            "and hand back the value *before*"],
           ["`Counter::increment` / `decrement` / `next`", "`(&var self) -> i64`",
            "by one"],
           ["`Counter::exchange`", "`(&var self, next: i64) -> i64`",
            "replace, handing back what was there"],
           ["`Counter::compareExchange`",
            "`(&var self, was: i64, want: i64) -> bool`",
            "store only while the value is still `was`"],
           ["`Counter::update`", "`(&var self, f: @cfunction(i64) -> i64) -> i64`",
            "apply `f`, retrying until it sticks"],
           ["`Counter::raiseTo`", "`(&var self, floor: i64) -> i64`",
            "keep the larger"],
           ["`Flag`", "`class`", "a `bool` several threads may set"],
           ["`Flag::raise`", "`(&var self) -> bool`",
            "set it, and say whether this call did — exactly one caller ever "
            "gets `true`"],
           ["`Flag::lower` / `isRaised`", "", "clear it, read it"]],
          caption="One number at a time, without a lock. For anything larger, "
                  "`thread::Mutex`."),

        H("std::time"),
        T(["Name", "Signature", "Does"],
          [["`Time`", "`struct`", "a length of time, to the nanosecond"],
           ["`nanoseconds` … `hours`", "`(count: i64) -> Time`",
            "build one by naming its unit"],
           ["`zero`", "`() -> Time`", "no time at all"],
           ["`Time::asMilliseconds` and friends", "`(&self) -> i64`",
            "the whole duration in that unit; also `asSecondsFloat`"],
           ["`Time::plus` / `minus` / `times`", "", "also `+` and `-`"],
           ["`Instant`", "`struct`", "a reading from a forward-only clock"],
           ["`now`", "`() -> Instant`", "the clock, now"],
           ["`Instant::since` / `elapsed`", "`-> Time`",
            "how long between two readings"]],
          caption="`Time` is one `i64`, so it is `Send` and `Sync` for the "
                  "same reason an `i64` is."),
        P("The calendar is the other half: dates, times of day, and moments "
          "on the wall clock, in the proleptic Gregorian calendar with the "
          "arithmetic done on a count of days since 1970-01-01. `utcNow` and "
          "`localNow` read the wall clock — which, unlike `now`, can be set "
          "and can jump, so it is for saying *when* rather than for "
          "measuring."),
        T(["Name", "Signature", "Does"],
          [["`Date`", "`struct { year, month, day }`",
            "a calendar date; `month` and `day` count from one"],
           ["`date`", "`(year, month, day) -> Date?`",
            "the date, or `nil` for a day that does not exist"],
           ["`dateFromDays`", "`(days: i64) -> Date`",
            "days since 1970-01-01, negative before it"],
           ["`Date::toDays`", "`(&self) -> i64`", "the inverse"],
           ["`Date::weekday`", "`(&self) -> Weekday`",
            "`Monday` … `Sunday`, an enum that prints as its name"],
           ["`Date::plusDays` / `plusMonths` / `plusYears`", "`(&self, i64) -> Date`",
            "arithmetic; a month lands clamped to its last day"],
           ["`Date::daysUntil`", "`(&self, other: Date) -> i64`",
            "signed distance in days"],
           ["`Date::dayOfYear` / `isLeapYear` / `next`", "",
            "the position in the year; the year's shape; the next given weekday"],
           ["`Date::iso`", "`(&self) -> String`",
            "`2026-09-11`; also what it prints as, and `<`, `==` compare dates"],
           ["`parseDate`", "`(String) -> Date?`", "`2026-09-11` read back"],
           ["`isLeapYear` / `daysInMonth`", "`(year) -> bool` / `(year, month) -> i64`",
            "the calendar's two facts"],
           ["`TimeOfDay`", "`struct { hour, minute, second, nanosecond }`",
            "a time of day; `timeOfDay(h, m, s)` checks one"],
           ["`DateTime`", "`struct { date, time, offset }`",
            "a moment: a date and time, `offset` seconds east of UTC"],
           ["`utcNow` / `localNow`", "`() -> DateTime`",
            "the wall clock, in UTC or this machine's zone"],
           ["`today`", "`() -> Date`", "today's date in this machine's zone"],
           ["`fromUnix` / `fromUnixNanos`", "`(i64) -> DateTime`",
            "seconds or nanoseconds since the epoch, in UTC"],
           ["`DateTime::toUnix` / `toUnixNanos`", "`(&self) -> i64`",
            "the inverse, whatever zone it is written in"],
           ["`DateTime::inUtc` / `inLocalZone` / `toOffset`", "`-> DateTime`",
            "the same moment, written in another zone"],
           ["`DateTime::plus` / `minus` / `since`", "",
            "arithmetic with a `Time`; `<` and `==` compare moments"],
           ["`DateTime::iso`", "`(&self) -> String`",
            "`2026-09-11T10:20:30Z`, or `+01:00` in a zone — RFC 3339"],
           ["`parseDateTime`", "`(String) -> DateTime?`",
            "the same read back; a bare date is midnight UTC"],
           ["`localZoneName`", "`() -> String`",
            "`BST`, `PDT` — whatever the platform calls it right now"]],
          caption="Dates compare and print; a `DateTime` in one zone equals "
                  "the same moment in another."),
        S("""import std::io
import std::time

fn main() -> i64 {
    let launch = time::date(2026, 9, 11) ?? time::dateFromDays(0)
    io::println(launch.weekday())
    io::println(launch.plusDays(30))
    io::println(launch.next(time::Weekday::Monday))

    let moment = time::parseDateTime("2026-09-11T10:20:30Z") ?? time::fromUnix(0)
    io::println(moment.plus(time::hours(25)))
    io::println(moment.toOffset(3600))
    io::println(time::fromUnix(1700000000))
    0
}""", mode="run", title="Dates and moments"),

        H("std::reflect"),
        T(["Function", "Signature", "Does"],
          [["`typeName`", "`<T>() -> String`", "the fully qualified name"],
           ["`typeId`", "`<T>() -> u64`", "an identity to compare"],
           ["`kindOf`", "`<T>() -> Kind`", "which shape `T` is"],
           ["`sizeOf` / `alignOf` / `strideOf`", "`<T>() -> usize`", "layout"],
           ["`offset_of!`", "`(T, field)`", "where a field begins"],
           ["`fieldCount`", "`<T>() -> usize`", "how many parts `T` has"],
           ["`fieldName` / `fieldType`", "`<T>(i: usize) -> String`",
            "the name of part *i*, or of its type"],
           ["`conforms`", "`<T, M>() -> bool`", "whether `T` binds mark `M`"],
           ["`isSend` / `isSync`", "`<T>() -> bool`",
            "whether `T` may cross or be shared between threads"],
           ["`describe`", "`<T>(value: T) -> String`",
            "a value rendered from its layout"]],
          caption="Answered while compiling; see **Compile-time reflection**."),

        H("std::fmt"),
        T(["Function", "Signature", "Does"],
          [["`show`", "`<T: Display>(value: T) -> String`", "what `{}` does"],
           ["`fixed`", "`(value: f64, places: i64) -> String`",
            "`{:.N}` — that many places, rounded"],
           ["`radix`", "`<T>(value: T, base: i64, upper: bool, prefix: bool) "
            "-> String`", "`{:x}`, `{:b}`, `{:o}`"],
           ["`plus`", "`(text: String) -> String`",
            "`{:+}` — a leading `+` where there is no sign"],
           ["`pad`", "`(text: String, width: i64, align: Character, "
            "fill: Character) -> String`",
            "`{:>8}` — widen to `width`, counting characters"]],
          caption="What a `format!` placeholder expands into. Each is an "
                  "ordinary function, usable on its own."),

        H("std::io"),
        T(["Function", "Signature", "Does"],
          [["`print`", "`<T: Display>(value: T)`", "writes to stdout"],
           ["`println`", "`<T: Display>(value: T)`", "and a newline"],
           ["`eprint`", "`<T: Display>(value: T)`", "writes to stderr"],
           ["`eprintln`", "`<T: Display>(value: T)`", "and a newline"],
           ["`debug`", "`<T: Display>(value: T)`", "stderr, prefixed"],
           ["`newline`", "`()`", "a bare newline"],
           ["`readLine`", "`() -> String`", "a line from stdin, newline "
            "removed"],
           ["`readLineOrEnd`", "`() -> String?`", "the same, and `nil` at the "
            "end of the input — which is what a loop needs"]],
          caption="`Display` is the public mark in this module; anything that "
                  "binds it can be printed."),
        P("Every builtin binds `Display`, and so do the *shapes*: "
          "`bind<T> Display to [T] where T: Display` covers every slice and "
          "every array of something printable, and there are pair and triple "
          "bindings beside it. So an array prints without anything having to "
          "be written for its element type — see "
          "[Binding a shape](#generics)."),
        S("""import std::io

fn main() -> i64 {
    io::println([1, 2, 3])
    io::println((7, "seven"))
    io::println([["a", "b"], ["c", "d"]])
    0
}""", mode="run", title="The shapes the library binds"),
        N("`readLine` cannot tell a blank line from the end of the input: "
          "both give an empty string. `readLineOrEnd` can, which is why it is "
          "the one to loop over — `while io::readLineOrEnd() is Some(line)` "
          "ends where the input does.", label="Reading until the end"),
        P("`println!` and `print!` live here too. They take a format string, "
          "need no import, and are what most code reaches for; the functions "
          "above are what they call. See **Formatting**."),
        S("""import std::io

struct Duration { seconds: i64 }

bind io::Display to Duration {
    fn display(&self) -> String {
        let m = self.seconds / 60
        let s = self.seconds % 60
        m.$str() + "m" + s.$str() + "s"
    }
}

fn main() -> i64 {
    io::println(Duration { seconds: 205 })
    io::print("no newline: ")
    io::println(42)
    io::eprintln("this went to stderr")
    0
}""", mode="run", title="Printing your own types"),

        H("std::testing"),
        P("Assertions for the programs under `tests/`. Each check prints its "
          "own line, so a failure names itself instead of leaving you to work "
          "out which of twenty assertions went wrong, and `summary()` returns "
          "what `main` should — which is the verdict `rune test` reads. There "
          "is a section of its own on [testing](#testing)."),
        T(["Function", "Passes when"],
          [["`equal(name, got, want)`", "the two render the same"],
           ["`notEqual(name, got, want)`", "they do not"],
           ["`isTrue(name, cond)` / `isFalse`", "the condition holds, or does "
            "not"],
           ["`isSome(name, opt)` / `isNone`", "the Option holds something, or "
            "nothing"],
           ["`unreachable(name)`", "never — for a branch that should not run"],
           ["`counts()`", "— returns `(passed, failed)`"],
           ["`summary()`", "— prints the tally, returns 0 or 1"]]),

        H("std::io: files"),
        P("Every file operation that can fail says so in its type, so there is "
          "no errno to remember and no return code to forget. A `File` closes "
          "itself when the last reference to it goes, so a program that never "
          "calls `close` still does not leak a descriptor."),
        T(["Member", "Signature", "Does"],
          [["`open`", "`(path: String) -> Result<File, FileError>`", "an "
            "existing file, for reading"],
           ["`create`", "`(path) -> Result<File, FileError>`", "a new one, or "
            "empties an old one"],
           ["`append`", "`(path) -> Result<File, FileError>`", "for writing at "
            "the end"],
           ["`readToString`", "`(path) -> Result<String, FileError>`", "the "
            "whole file, in one call"],
           ["`writeString`", "`(path, text) -> Result<i64, FileError>`", "the "
            "file's entire contents"],
           ["`appendString`", "`(path, text) -> Result<i64, FileError>`", ""],
           ["`exists`", "`(path) -> bool`", "can it be opened?"],
           ["`delete`", "`(path) -> FileError?`", "`nil` means it is gone"],
           ["`describe`", "`(e: FileError) -> String`", "what went wrong, in "
            "words"]]),
        T(["Directories", "Signature", "Does"],
          [["`isDirectory`", "`(path) -> bool`", "false for a file, and for "
            "nothing at all"],
           ["`readDirectory`",
            "`(path) -> Result<Vector<DirEntry>, FileError>`",
            "one level, in the filesystem\u2019s own order"],
           ["`walkDirectory`", "`(path, depth: i64) -> Vector<String>`",
            "every file underneath, as paths relative to `path`"],
           ["`makeDirectory`", "`(path) -> FileError?`",
            "succeeds when it is already there"],
           ["`makeDirectories`", "`(path) -> FileError?`",
            "and every missing parent above it"],
           ["`joinPath`", "`(left, right) -> String`",
            "exactly one separator, and none for an empty side"]],
          caption="A `DirEntry` is a `name` and an `isDirectory`. The name is "
                  "the entry alone, not a path \u2014 `joinPath` reaches it. "
                  "`.` and `..` never appear."),
        S("""import std::io

fn main() -> i64 {
    let dir = "/tmp/rune_docs_tree/inner"
    io::makeDirectories(dir)
    io::writeString(io::joinPath(dir, "a.txt"), "a")
    io::writeString("/tmp/rune_docs_tree/b.txt", "b")

    // One level: files and folders together, told apart by `isDirectory`.
    match io::readDirectory("/tmp/rune_docs_tree") {
        Ok(entries) => {
            for e in entries {
                io::println((if e.isDirectory { "dir  " } else { "file " }) + e.name)
            }
        },
        Err(e) => io::println(io::describe(e)),
    }

    // The whole tree, files only, relative to where the walk started.
    for path in io::walkDirectory("/tmp/rune_docs_tree", 4) {
        io::println("  " + path)
    }

    io::delete(io::joinPath(dir, "a.txt"))
    io::delete("/tmp/rune_docs_tree/b.txt")
    0
}""", mode="run", title="Walking a tree"),
        N("The order `readDirectory` hands entries back in is the "
          "filesystem\u2019s, which is neither alphabetical nor the same on two "
          "machines. Sort the result when the order matters.",
          label="Order is not promised", tone="warn"),
        T(["On a `File`", "Signature", "Does"],
          [["`readLine`", "`(&var self) -> String?`", "next line, newline "
            "removed; nothing at end of file"],
           ["`readAll`", "`(&var self) -> String`", "everything left"],
           ["`write`", "`(&var self, text) -> Result<i64, FileError>`", "bytes "
            "written"],
           ["`writeLine`", "`(&var self, text) -> Result<i64, FileError>`", ""],
           ["`flush`", "`(&var self)`", "push buffered writes out"],
           ["`close`", "`(&var self)`", "now rather than later; twice is "
            "harmless"],
           ["`isOpen`", "`(&self) -> bool`", ""],
           ["`name`", "`(&self) -> String`", "the path it was opened under"]]),
        S(r"""import std::io

fn main() -> i64 {
    let path = "/tmp/rune_docs_example.txt"

    match io::writeString(path, "first line\nsecond line\n") {
        Ok(n) => io::println("wrote " + n.$str() + " bytes"),
        Err(e) => io::println("write failed: " + io::describe(e)),
    }

    // A line at a time, until there are none left.
    match io::open(path) {
        Ok(f) => {
            var n = 0
            while f.readLine() is Some(line) {
                n += 1
                io::println("  " + n.$str() + ": " + line)
            }
        },
        Err(e) => io::println("open failed: " + io::describe(e)),
    }

    io::delete(path)
    0
}""", mode="run", title="Writing, then reading line by line"),
        P("A failure is a value, so it can be answered where it happens or "
          "passed on with `?` like any other `Result`."),
        S(r"""import std::io

/// Reads a file and counts its lines, or explains why it could not.
fn countLines(path: String) -> Result<i64, io::FileError> {
    let text = io::readToString(path)?
    var lines = 0
    var i = 0
    while i < text.$length() {
        if text.$byteAt(i) == 10 { lines += 1 }
        i += 1
    }
    Ok(lines)
}

fn main() -> i64 {
    let path = "/tmp/rune_docs_count.txt"
    io::writeString(path, "a\nb\nc\n")

    match countLines(path) {
        Ok(n) => io::println(n.$str() + " lines"),
        Err(e) => io::println("failed: " + io::describe(e)),
    }
    match countLines("/tmp/rune_docs_absent") {
        Ok(n) => io::println("unexpected"),
        Err(e) => io::println("missing: " + io::describe(e)),
    }

    io::delete(path)
    0
}""", mode="run", title="`?` carries a file error out"),
        N("`FileError` names the cases worth telling apart — `NotFound`, "
          "`PermissionDenied`, `AlreadyExists`, `IsDirectory`, "
          "`TooManyOpenFiles`, `Closed` and `Other` — so a caller can match on "
          "the one it wants to handle rather than parsing a message.",
          label="The error cases"),

        H("std::io: bytes and streams"),
        P("A `String` is text: UTF-8, and NUL-terminated for the C boundary. "
          "That makes it the wrong thing to read a socket into, because what "
          "arrived is whatever the other end sent. `Bytes` is a growable run "
          "of bytes, and it is what the stream marks move."),
        T(["Member", "Signature", "Does"],
          [["`Bytes`", "`class`", "a growable byte buffer over `mem::allocator`"],
           ["`length` / `isEmpty`", "`(&self)`", ""],
           ["`at`", "`(&self, index: i64) -> u8?`", "nothing past the end"],
           ["`push`", "`(&var self, value: u8)`", "one byte"],
           ["`append` / `appendText`",
            "`(&var self, Bytes)` / `(&var self, String)`",
            "another buffer, or a string's UTF-8"],
           ["`slice`", "`(&self, from: i64, upto: i64) -> Bytes`",
            "a buffer of its own"],
           ["`consume`", "`(&var self, n: i64)`",
            "drops the first `n`, moving the rest to the front"],
           ["`find`", "`(&self, needle: String, from: i64) -> i64`",
            "where it starts, or `-1`"],
           ["`toString`", "`(&self) -> String`",
            "the contents read as text, as they are"],
           ["`raw` / `writePoint` / `advance`", "—",
            "the block itself, for the C boundary"],
           ["`bytes`", "`(text: String) -> Bytes`", "a buffer holding it"]],
          caption="`Bytes` binds `Display`, so it prints as its own text."),
        P("Two marks describe where bytes come from and where they go, and a "
          "third is both. Everything else in the library is written against "
          "them rather than against a file or a socket, so the same code works "
          "over either."),
        T(["Member", "Signature", "Does"],
          [["`Reader`", "`mark`", "somewhere bytes come from"],
           ["`read`",
            "`(&var self, sink: Bytes, most: i64) -> Result<i64, StreamError>`",
            "the one thing to supply; `0` means finished"],
           ["`readAll`", "`(&var self, sink: Bytes) -> Result<i64, …>`",
            "everything left"],
           ["`readExact`", "`(&var self, sink: Bytes, count: i64) -> …`",
            "exactly that many; short is a failure"],
           ["`Writer`", "`mark`", "somewhere bytes go"],
           ["`write`", "`(&var self, data: Bytes) -> Result<i64, …>`",
            "what it can; short is not a failure"],
           ["`writeAll` / `writeText` / `writeLine`", "—",
            "every byte, however many calls that takes"],
           ["`flush`", "`(&var self) -> Result<i64, …>`",
            "push anything buffered out"],
           ["`Stream`", "`mark Stream: Reader + Writer`",
            "both at once — a socket, a pipe"],
           ["`BufferedReader<R: Reader>`", "`class`",
            "a reader with a buffer in front of it, so `readLine` costs one "
            "call rather than one per byte"],
           ["`copy`", "`<R: Reader, W: Writer>(&var R, &var W) -> …`",
            "everything from one to the other"],
           ["`File::asStream`", "`(&self) -> FileStream`",
            "a file in the shape the marks take"]],
          caption="`StreamError` is `Closed`, `WouldBlock`, `Interrupted`, "
                  "`TimedOut` or `Failed`; `describeStream` puts it in words."),
        N("`File` has a `write` and a `flush` of its own, taking text and "
          "reporting a `FileError`. `Stream` asks for the same two names with "
          "different shapes, and Rune has one name per method — so a file is "
          "*turned into* a stream rather than being one. `asStream` costs an "
          "object, not a copy of the file, and both views share the handle.",
          label="Why `asStream` and not `File` itself"),
        S(r"""import std::io

fn main() -> i64 {
    var b = io::Bytes()
    b.appendText("hello, ")
    b.appendText("world")
    io::println(b)
    io::println(b.slice(0, 5).toString())
    io::println(b.find("world", 0))

    let path = "/tmp/rune_docs_stream.txt"
    match io::create(path) {
        Ok(f) => {
            var out = f.asStream()
            out.writeLine("alpha")
            out.writeLine("beta")
            out.flush()
        },
        Err(e) => { io::println("create failed"); return 1 },
    }

    // `BufferedReader` works over anything that reads — a file here, a
    // socket in `std::net`.
    match io::open(path) {
        Ok(f) => {
            var lines = io::BufferedReader<io::FileStream>(f.asStream())
            while lines.readLine() is Some(line) { io::println("  " + line) }
        },
        Err(e) => { io::println("open failed"); return 1 },
    }
    io::delete(path)
    0
}""", mode="run", title="Bytes, a file as a stream, and lines"),

        H("std::net"),
        P("TCP, as streams. `listen` and `connect` are the two ways in, and "
          "what comes back is an `io::Stream` — so `readLine`, `writeText`, "
          "`BufferedReader` and `io::copy` all work over a socket without "
          "knowing it is one."),
        T(["Member", "Signature", "Does"],
          [["`listen`",
            "`(address: String, port: i32) -> Result<TcpListener, NetError>`",
            "port `0` asks the system to pick one"],
           ["`listenWith`", "`(address, port, backlog: i32) -> …`",
            "the same, with the backlog said out loud"],
           ["`connect`",
            "`(host: String, port: i32) -> Result<TcpStream, NetError>`",
            "a name or a dotted address; the platform resolves it"],
           ["`TcpListener::accept`",
            "`(&var self) -> Result<TcpStream, NetError>`",
            "waits for the next connection"],
           ["`TcpListener::port`", "`(&self) -> i32`",
            "the port it actually got"],
           ["`TcpStream`", "`struct`, binds `io::Stream`",
            "one end of a connection"],
           ["`peerAddress` / `port`", "`(&self)`", "who is at the other end"],
           ["`setNoDelay`", "`(&var self, on: bool) -> NetError?`",
            "send small writes immediately"],
           ["`setReadTimeout` / `setWriteTimeout`",
            "`(&var self, milliseconds: i64) -> NetError?`",
            "zero waits for ever, which is the default"],
           ["`shutdown`", "`(&var self, how: Shutdown) -> NetError?`",
            "`Read`, `Write` or `Both`"],
           ["`close`", "`(&var self)`", "now rather than later"],
           ["`release` / `adopt`", "`(&var self) -> i64` / `(i64) -> TcpStream`",
            "hand the descriptor over, and take one"],
           ["`clone`", "`(&self) -> Self`", "a second descriptor for the "
            "same socket; what `$clone()` does"],
           ["`AsyncStream` / `AsyncListener`", "`class`",
            "the same sockets for tasks: `read`, `write`, `writeText`, "
            "`readAll` and `accept` are `async fn`s that park the task on "
            "the socket"],
           ["`listenAsync`", "`(address: String, port: i32) -> Result<AsyncListener, NetError>`",
            "listen, for tasks"],
           ["`connectAsync`", "`(host: String, port: i32) -> Future<Result<AsyncStream, NetError>>`",
            "connect without holding the thread"],
           ["`wrap`", "`(stream: TcpStream) -> AsyncStream`",
            "drive an existing connection with tasks"]],
          caption="`NetError` is `Refused`, `AddressInUse`, `Unreachable`, "
                  "`WouldBlock`, `Interrupted`, `TimedOut`, `Reset`, "
                  "`PermissionDenied`, `NotFound`, `Closed` or `Failed`; "
                  "`describe` puts it in words. See **Tasks and futures** "
                  "for the `Async` pair."),
        P("Both types own their descriptor the way [Structs](#structs) "
          "describes: the field is `@resource`, the `deinit` closes it, and "
          "handing one on is a move. So a connection closes itself when the "
          "binding holding it goes, and there is never a second owner to "
          "close it twice."),
        S(r"""import std::io
import std::net
import std::thread
import std::time

/// The port the server ended up on, so the client knows where to knock.
global port: thread::Mutex<i64> = thread::Mutex<i64>(0)

fn serve(unused: i64) -> i64 {
    var listener = match net::listen("127.0.0.1", 0i32) {
        Ok(l) => l,
        Err(e) => { io::eprintln("listen: " + net::describe(e)); return 1 },
    }
    var slot = port
    slot.set(listener.port() as i64)

    match listener.accept() {
        Ok(c) => {
            var conn = c
            // A socket is a stream, so this is the same reader a file gets.
            var lines = io::BufferedReader<net::TcpStream>(conn)
            match lines.readLine() {
                Some(line) => io::println("server got: " + line),
                None => io::println("server got nothing"),
            }
            lines.writeText("pong\n")
            0
        },
        Err(e) => { io::eprintln("accept: " + net::describe(e)); 1 },
    }
}

fn main() -> i64 {
    var server = thread::spawn(serve, 0)

    var slot = port
    var waited = 0
    while slot.get() == 0 && waited < 200 {
        thread::sleep(time::milliseconds(10))
        waited += 1
    }

    match net::connect("127.0.0.1", slot.get() as i32) {
        Ok(c) => {
            var conn = c
            conn.setNoDelay(true)
            conn.writeText("ping\n")
            var lines = io::BufferedReader<net::TcpStream>(conn)
            match lines.readLine() {
                Some(line) => io::println("client got: " + line),
                None => io::println("client got nothing"),
            }
        },
        Err(e) => io::eprintln("connect: " + net::describe(e)),
    }
    server.join()
    0
}""", mode="run", title="A connection over the loopback"),
        N("A `TcpStream` must not be put in a container or sent through a "
          "channel: both store what they carry through memory the compiler "
          "cannot follow, so there would be two copies of one descriptor and "
          "nothing to say which owns it. `release` hands the descriptor out "
          "as a plain `i64` — nothing owns it, so nothing can close it — and "
          "`adopt` takes it back. `examples/project/webserver` is a worker "
          "pool built that way.", label="Crossing a boundary", tone="warn"),

        H("std::option"),
        P("Every method below is an ordinary method on an ordinary enum. The "
          "sugar — `T?`, `nil`, `??`, `?` — is only in the spellings."),
        T(["Member", "Signature", "Does"],
          [["`Option<T>`", "`enum { Some(T), None }`", "a value or nothing"],
           ["`hasValue`", "`(&self) -> bool`", "true when `Some`"],
           ["`isNil`", "`(&self) -> bool`", "true when `None`"],
           ["`isSuchThat`", "`(&self, @function(T) -> bool) -> bool`",
            "true when present *and* it passes"],
           ["`or`", "`(&self, fallback: T) -> T`", "the value, or the fallback"],
           ["`orElse`", "`(&self, @function() -> T) -> T`",
            "same, producing the fallback only if needed"],
           ["`unwrap`", "`(&self) -> T`", "the value; **aborts** on `None`"],
           ["`expect`", "`(&self, message: CString) -> T`", "same, with your "
            "message"],
           ["`map`", "`<U>(&self, @function(T) -> U) -> Option<U>`",
            "the value transformed; empty passes through"],
           ["`mapOr`", "`<U>(&self, fallback: U, @function(T) -> U) -> U`",
            "`map` then `or`, in one step"],
           ["`andThen`",
            "`<U>(&self, @function(T) -> Option<U>) -> Option<U>`",
            "`map` for a function that may itself come up empty"],
           ["`filter`", "`(&self, @function(T) -> bool) -> Option<T>`",
            "the value, but only if it passes"],
           ["`otherwise`", "`(&self, other: Option<T>) -> Option<T>`",
            "this one if present, else the other; stays an `Option`"],
           ["`zip`", "`<U>(&self, Option<U>) -> Option<(T, U)>`",
            "both as a pair, or nothing"],
           ["`take`", "`(&var self) -> Option<T>`",
            "hands the value over and leaves this one empty"],
           ["`replace`", "`(&var self, value: T) -> Option<T>`",
            "stores one, returns what was there"],
           ["`clear`", "`(&var self)`", "empties it"],
           ["`some` / `none`", "`<T>(...) -> Option<T>`", "constructors"],
           ["`when`", "`<T>(bool, T) -> Option<T>`",
            "the value if the condition holds"],
           ["`flatten`", "`<T>(Option<Option<T>>) -> Option<T>`",
            "one out of two"]]),
        S("""import std::io

fn main() -> i64 {
    let found: i64? = 7

    // A chain of these reads as one calculation rather than four nil checks:
    // the empty case passes straight through every step.
    io::println(found.map(||(v: i64) -> String { "got " + v.$str() }) ?? "none")
    io::println((found.filter(||(v: i64) -> bool { v > 100 }) ?? -1).$str())
    io::println(found.mapOr(0, ||(v: i64) -> i64 { v * 2 }).$str())

    // `take` empties as it hands over, which is how a field that owns
    // something is moved out of.
    var pending: String? = "job"
    let claimed = pending.take()
    io::println((claimed ?? "-") + " / " + (pending ?? "-"))
    0
}""", mode="run", title="Option without unwrapping it"),

        H("std::result"),
        T(["Member", "Signature", "Does"],
          [["`Result<T, E>`", "`enum { Ok(T), Err(E) }`", "success or failure"],
           ["`isOk` / `isErr`", "`(&self) -> bool`", "which one it is"],
           ["`isSuchThat`", "`(&self, @function(T) -> bool) -> bool`",
            "succeeded *and* the value passes"],
           ["`or`", "`(&self, fallback: T) -> T`", "the value, or the fallback"],
           ["`orElse`", "`(&self, @function() -> T) -> T`",
            "same, producing the fallback only if needed"],
           ["`recover`", "`(&self, @function(E) -> T) -> T`",
            "the value, or what the error is turned into"],
           ["`unwrap`", "`(&self) -> T`", "the value; **aborts** on `Err`"],
           ["`expect`", "`(&self, message: CString) -> T`", "same, with your "
            "message"],
           ["`unwrapErr`", "`(&self) -> E`", "the error; **aborts** on `Ok`"],
           ["`map`", "`<U>(&self, @function(T) -> U) -> Result<U, E>`",
            "the value transformed; an error passes through"],
           ["`mapErr`", "`<F>(&self, @function(E) -> F) -> Result<T, F>`",
            "the error transformed — a conversion local to one call, when "
            "a `bind As` would be too wide"],
           ["`andThen`",
            "`<U>(&self, @function(T) -> Result<U, E>) -> Result<U, E>`",
            "what `?` does, without the early return"],
           ["`ok`", "`(&self) -> Option<T>`", "the success as an `Option`"],
           ["`error`", "`(&self) -> Option<E>`", "the failure as an `Option`"],
           ["`ok` / `err`", "`<T, E>(...) -> Result<T, E>`", "constructors"],
           ["`from`", "`<T, E>(Option<T>, error: E) -> Result<T, E>`",
            "an empty lookup made into one that says why"],
           ["`flatten`", "`<T, E>(Result<Result<T, E>, E>) -> Result<T, E>`",
            "one out of two"]]),
        N("`?` hands the error out as the function's own error type. When "
          "the two differ, `bind Theirs into Ours` (the same as "
          "`bind As<Ours> to Theirs`) says how to get from "
          "one to the other and `?` calls its `convert` on the way out — the "
          "same `As` that `into` dispatches through. `mapErr` is still there "
          "for a conversion that is local to one call. See [`?` converts the "
          "error](#option-result).",
          label="Errors convert through `As`"),
        S("""import std::io

fn halve(n: i64) -> Result<i64, String> {
    if n % 2 != 0 { return Err(n.$str() + " is odd") }
    Ok(n / 2)
}

fn main() -> i64 {
    io::println(halve(84).unwrap().$str())
    io::println(halve(7).or(-1).$str())
    io::println(halve(7).error().or("none").$str())
    io::println(halve(84).ok().hasValue().$str())
    0
}""", mode="run", title="Result in practice"),

        H("std::iter"),
        P("The two marks `for` dispatches through, and everything they carry. "
          "Both marks are in scope everywhere, and every adaptor a `Sequence` "
          "has is the `Iterator` one with an `iterate` in front of it; see "
          "[Iterators](#iterators)."),
        N("A function that returns a chain writes `-> some Iterator` rather "
          "than the nested wrapper type. The adaptors themselves still return "
          "`Map<Self, B>` and friends — `some` is how a caller is spared from "
          "writing those out. See [Opaque results: `some Mark`](#marks).",
          label="Returning a chain"),
        T(["Member", "Signature", "Does"],
          [["`Iterator`", "`mark { type Item; next; … }`",
            "a cursor over values"],
           ["`next`", "`(&var self) -> Self::Item?`",
            "the next value, `nil` at the end — the one thing to supply"],
           ["`map`", "`<B>(self, @function(Self::Item) -> B) -> Map<Self, B>`",
            "every value, with `f` applied"],
           ["`filter`",
            "`(self, @function(Self::Item) -> bool) -> Filter<Self>`",
            "only the values `keep` says yes to"],
           ["`zip`", "`<J: Iterator>(self, J) -> Zip<Self, J>`",
            "pairs, ending with the shorter"],
           ["`take_while`",
            "`(self, @function(Self::Item) -> bool) -> TakeWhile<Self>`",
            "up to the first `false`; the source is not asked again"],
           ["`skip_while`",
            "`(self, @function(Self::Item) -> bool) -> SkipWhile<Self>`",
            "everything from the first `false` onwards"],
           ["`take` / `skip`", "`(self, count: i64) -> Take<Self>` / `Skip<Self>`",
            "at most `count`, or all but the first `count`"],
           ["`enumerate`", "`(self) -> Enumerate<Self>`",
            "each value paired with its position"],
           ["`chain`", "`<J: Iterator>(self, J) -> Chain<Self, J>`",
            "this one's values, then the other's"],
           ["`flatten`", "`(self) -> Flatten<Self> where Self::Item: Iterator`",
            "every value of every inner iterator, laid out flat"],
           ["`flatMap`",
            "`<J: Iterator>(self, @function(Self::Item) -> J) -> Flatten<Map<Self, J>>`",
            "`map` and `flatten` in one"],
           ["`as_iter`", "`(self) -> Self`",
            "itself; a `Sequence`'s hands out a cursor instead"],
           ["`collect`", "`(self) -> vector::Vector<Self::Item>`",
            "runs the chain out into a vector"],
           ["`count`", "`(self) -> i64`", "how many are left"],
           ["`find`",
            "`(self, @function(Self::Item) -> bool) -> Self::Item?`",
            "the first match, stopping there"],
           ["`any` / `all`",
            "`(self, @function(Self::Item) -> bool) -> bool`",
            "stopping at the first yes, or the first no"],
           ["`step_by`", "`(self, stride: i64) -> StepBy<Self>`",
            "every `stride`-th value"],
           ["`inspect`",
            "`(self, @function(Self::Item) -> ()) -> Inspect<Self>`",
            "every value unchanged, with a look at each on the way past"],
           ["`forEach`", "`(self, @function(Self::Item) -> ())`",
            "every value, one at a time, with nothing kept"],
           ["`fold`",
            "`<A>(self, initial: A, @function(A, Self::Item) -> A) -> A`",
            "the general form of every terminator above"],
           ["`reduce`",
            "`(self, @function(Self::Item, Self::Item) -> Self::Item) "
            "-> Self::Item?`",
            "`fold` with the first value as the seed"],
           ["`last`", "`(self) -> Self::Item?`", "the last one; walks to the "
            "end"],
           ["`nth`", "`(self, index: i64) -> Self::Item?`",
            "the one at `index`, counting from zero"],
           ["`position`",
            "`(self, @function(Self::Item) -> bool) -> i64?`",
            "where the first match is, rather than what it is"],
           ["`best`",
            "`(self, @function(Self::Item, Self::Item) -> bool) "
            "-> Self::Item?`",
            "the value the comparison prefers over every other"],
           ["`Sequence`", "`mark { type Iter: Iterator; iterate; … }`",
            "something a cursor can be had from; carries all of the above"],
           ["`iterate`", "`(&self) -> Self::Iter`", "a fresh cursor"],
           ["`counting` / `countingBy`", "`(i64[, i64]) -> Counter`",
            "integers, endlessly"],
           ["`vector::collect`", "`<I: Iterator>(I) -> Vector<I::Item>`",
            "`collect` written the other way round"],
           ["`vector::VectorIter<T>`", "`Iterator`",
            "what `Vector<T>` hands out"],
           ["`text::chars`", "`(String) -> Chars`",
            "the characters of a String, in one pass"]]),
        S("""import std::io
import std::iter
import std::collections::vector

fn main() -> i64 {
    let scores = vec!(3, 1, 4, 1, 5)

    // `fold` is the general form: a running answer and a step. `count`,
    // `any` and `all` are all folds with the step already written.
    io::println(scores.as_iter().fold(0, ||(sum: i64, n: i64) -> i64 {
        sum + n
    }).$str())

    // `reduce` seeds itself from the first value, so an empty chain has an
    // answer — `nil` — rather than needing one invented.
    io::println((scores.values().reduce(||(a: i64, b: i64) -> i64 {
        if a > b { a } else { b }
    }) ?? -1).$str())

    // `find` hands back the value; `position` hands back where it was.
    io::println((scores.as_iter().position(||(n: i64) -> bool { n == 4 }) ?? -1).$str())
    io::println((scores.as_iter().nth(2) ?? -1).$str())

    // `best` takes the comparison rather than requiring a bound: it returns
    // true when its first argument should win.
    io::println((scores.as_iter().best(||(a: i64, b: i64) -> bool { a < b }) ?? -1).$str())

    // `inspect` is the chain's own print statement. The chain is lazy, so
    // only the two values `take` asks for ever go past it.
    io::println(scores.as_iter().inspect(||(n: i64) -> () {
        io::println("saw " + n.$str())
    }).take(2).count().$str())

    for n in iter::counting(0).step_by(5).take(4) { io::println(n.$str()) }
    0
}""", mode="run", title="Ending a chain"),

        H("std::any"),
        P("`Any` is a builtin type, so these need no import; the module is "
          "where they are written down. See [`Any`](#any) for what they mean."),
        T(["Member", "Signature", "Does"],
          [["`typeName`", "`(&self) -> String`",
            "the qualified name of the type inside"],
           ["`holds`", "`<T>(&self) -> bool`", "true when the value is a `T`"],
           ["`get`", "`<T>(&self) -> T?`",
            "the value as a `T`, or `nil`"],
           ["`expect`", "`<T>(&self) -> T`",
            "the value as a `T`; **aborts** on any other type"]]),

        H("std::math"),
        T(["Group", "Members"],
          [["constants", "`PI`, `E`, `TAU`, `SQRT_2`, `LN_2`, `LN_10`, "
            "`EPSILON`, `F64_MAX`, `F64_MIN_POSITIVE`"],
           ["powers and roots", "`squareRoot`, `cubeRoot`, `power`, "
            "`lengthOf`, `exponential`"],
           ["logarithms", "`naturalLog`, `log2Of`, `log10Of`, `logOf`"],
           ["trigonometry", "`sine`, `cosine`, `tangent`, `arcSine`, "
            "`arcCosine`, `arcTangent`, `angleOf`, `hyperbolicSine`, "
            "`hyperbolicCosine`, `hyperbolicTangent`, `radians`, `degrees`"],
           ["rounding", "`roundDown`, `roundUp`, `roundNearest`, `wholePart`, "
            "`fractionPart`, `remainderOf`, `withSignOf`"],
           ["asking about a float", "`isNan`, `isInfinite`, `isFinite`, "
            "`nearlyEqual`, `infinity`, `nan`"],
           ["comparison, for anything ordered",
            "`least`, `greatest`, `clamp` — bounded on `operator::cmp`"],
           ["magnitude and sign", "`absInt`, `absFloat`, `signInt`, "
            "`signFloat`"],
           ["exact integer answers", "`powerInt`, `squareRootInt`, `gcd`, "
            "`lcm`, `divFloor`, `modFloor`"],
           ["fixed-width shorthands", "`minInt`, `maxInt`, `minFloat`, "
            "`maxFloat`, `clampInt`"]],
          caption="Everything takes and returns `f64` unless its name says "
                  "`Int`. Needs `-l m`, which `rune` adds for you."),
        S("""import std::io
import std::math

fn main() -> i64 {
    io::println(math::squareRoot(2.0).$str())
    io::println(math::roundUp(math::PI).$str())

    // The comparisons are bounded on an operator rather than on a mark, so
    // they work for anything `<` works on — builtin or your own.
    io::println(math::least(3, 5).$str())
    io::println(math::greatest(2.5, 1.5).$str())
    io::println(math::least("apple", "banana"))
    io::println(math::clamp(120, 0, 100).$str())

    // The integer versions give exact answers where the float ones round.
    io::println(math::squareRootInt(50).$str())      // 7*7 <= 50 < 8*8
    io::println(math::powerInt(3, 5).$str())
    io::println(math::gcd(12, 18).$str())

    // `/` truncates towards zero; `divFloor` goes towards negative infinity,
    // which is what indexing a grid at negative coordinates wants.
    io::println((-7 / 2).$str() + " vs " + math::divFloor(-7, 2).$str())

    // NaN is the one value not equal to itself, so `== nan()` never works.
    io::println(math::isNan(math::nan()).$str())
    io::println(math::nearlyEqual(0.1 + 0.2, 0.3, 1.0e-9).$str())
    0
}""", mode="run", title="Numerics"),

        H("std::process"),
        T(["Function", "Signature", "Does"],
          [["`argCount`", "`() -> i64`", "argument count, program name "
            "included"],
           ["`arg`", "`(index: i64) -> String`",
            "one argument; empty when out of range"],
           ["`argument`", "`(index: i64) -> String?`",
            "the same, but absent and empty are told apart"],
           ["`programName`", "`() -> String`", "argument 0"],
           ["`args` / `arguments`", "`() -> Vector<String>`",
            "all of them, with and without the program name"],
           ["`exit`", "`(code: i32) -> Never`", "stops now; no deinits run"],
           ["`succeed` / `fail`", "`() -> Never`", "`exit(0)` and `exit(1)`"],
           ["`panic`", "`<M: io::Display>(message: M) -> Never`",
            "aborts with your message — a literal, a built `String`, or "
            "anything else printable"],
           ["`assert`", "`<M: io::Display>(condition: bool, message: M)`",
            "panics if false"],
           ["`liveObjectCount`", "`() -> i64`", "objects the runtime is still "
            "counting"]]),
        N("`exit`, `succeed`, `fail` and `panic` are declared `-> Never`. A "
          "`Never` converts to any type, so a call to one can stand wherever "
          "a value was expected. See [`Never`: a function that does not "
          "return](#functions).",
          label="They do not return"),
        N("`panic` and `assert` are generic over `io::Display` rather than "
          "overloaded. A free function cannot be overloaded in Rune — only a "
          "`bind` can — and a bound says what it requires instead of listing "
          "what it accepts, which is the better answer here anyway.",
          label="Why a bound rather than two functions"),
        S("""import std::io
import std::process

fn main() -> i64 {
    io::println("invoked as " + process::arg(0))
    io::println("argument count " + process::argCount().$str())
    process::assert(process::argCount() >= 1, "there is always argv[0]")
    io::println("still live: " + process::liveObjectCount().$str())
    0
}""", mode="run", title="The process itself"),

        H("std::env"),
        P("The process's environment: its variables, and where it is. A "
          "variable that is not set is `nil`; one that is set but empty is an "
          "empty `String` — a shell script tells the two apart, so this does "
          "too."),
        T(["Function", "Signature", "Does"],
          [["`get`", "`(name: String) -> String?`", "the value, or `nil` when unset"],
           ["`getOr`", "`(name: String, fallback: String) -> String`",
            "the value, or `fallback` when unset *or empty*"],
           ["`has`", "`(name: String) -> bool`", "set at all, even to nothing"],
           ["`set`", "`(name: String, value: String) -> bool`",
            "for this process and anything it starts"],
           ["`remove`", "`(name: String) -> bool`", "unsets it"],
           ["`all`", "`() -> Vector<(String, String)>`",
            "every variable, as `(name, value)` pairs"],
           ["`currentDirectory`", "`() -> String?`",
            "the working directory, if it can be read"],
           ["`setCurrentDirectory`", "`(path: String) -> bool`", "changes it"],
           ["`home`", "`() -> String?`", "`HOME`, or `USERPROFILE` on Windows"],
           ["`temporaryDirectory`", "`() -> String`",
            "`TMPDIR`, `TEMP` or `TMP`, else `/tmp`"]]),
        S("""import std::io
import std::env

fn main() -> i64 {
    let editor = env::getOr("EDITOR", "vi")
    io::println(editor.$isEmpty())
    env::set("GREETING", "hello")
    io::println(env::get("GREETING") ?? "unset")
    env::remove("GREETING")
    io::println(env::get("GREETING") ?? "unset")
    io::println(env::has("PATH"))
    0
}""", mode="run", title="Reading and writing the environment"),

        H("std::arch"),
        P("What the machine being built **for** is like. Everything here is "
          "settled while compiling, out of the target triple: a type alias is "
          "the type it names, and a number is in the object file as that "
          "number. `arch::bits` is the constant `64` on a 64-bit target, not "
          "something worked out at startup, so `if arch::is32Bit { ... }` "
          "folds away entirely on the other one."),
        P("A cross build answers for the target, never for the machine doing "
          "the compiling — which is the whole reason to ask here rather than "
          "at run time."),
        T(["Name", "Is", "On a 64-bit target"],
          [["`size`", "`type`", "`i64` — signed, pointer-sized: an offset, "
            "a difference, an index"],
           ["`usize`", "`type`", "`u64` — unsigned, pointer-sized: a length "
            "or a count, and what `mem::size_of` hands back"],
           ["`float`", "`type`", "`f64` — a *size* rule, not a speed one"],
           ["`bits`", "`i64`", "`64`"],
           ["`pointerSize`", "`i64`", "`8`"],
           ["`is64Bit` / `is32Bit`", "`bool`", "`true` / `false`"],
           ["`endian`", "`String`", "`\"little\"` or `\"big\"`"],
           ["`littleEndian` / `bigEndian`", "`bool`", "the same, to branch on"],
           ["`name`", "`String`", "`aarch64`, `x86_64`, `x86`, `arm`, "
            "`riscv32`, `riscv64`, `wasm32`, `wasm64`, `powerpc64`, or "
            "`unknown`"],
           ["`os`", "`String`", "`macos`, `windows`, `linux`, `ios`, "
            "`android`, `freebsd`, `openbsd`, `netbsd`, `solaris`, `wasi`, or "
            "`unknown`"],
           ["`family`", "`String`", "`unix`, `windows` or `wasm`"],
           ["`triple()`", "`fn -> String`", "the whole triple as LLVM "
            "normalised it — `arm64-apple-macosx15.0.0`, "
            "`x86_64-w64-windows-gnu`"]]),
        S("""import std::io
import std::arch

fn main() -> i64 {
    // A type alias *is* the type, so this is the machine word.
    let index: arch::size = 3
    let count: arch::usize = 10 as arch::usize
    io::println((index + 1).$str())
    io::println(count.$str())

    io::println(arch::bits.$str() + "-bit " + arch::name)
    io::println(arch::os + " (" + arch::family + ")")
    io::println(arch::endian + "-endian")

    // Folded to one branch: the other is not in the binary at all.
    if arch::is64Bit { io::println("wide pointers") }
    else { io::println("narrow pointers") }
    0
}""", mode="run", title="Asking about the target"),
        P("`triple()` is the one answer that is not a fixed list, so it is the "
          "compiler's own rather than a `@Config` branch. It still costs "
          "nothing: the string is in the object file."),
        N("These are the same answers `@Config` gives, in a form you can "
          "compute with. Reach for `@Config` when a declaration should not "
          "**exist** on a target — a function that calls something only "
          "Windows has — and for `std::arch` when a value or a type "
          "depends on it.", label="`@Config` or `std::arch`?"),

        H("std::random"),
        P("A `Random` is a generator with its own state. Seeded from a number "
          "it repeats exactly, which is what a test or a simulation wants; "
          "seeded from the operating system with `new` it does not. The "
          "generator is xoshiro256** over SplitMix64 — fast and good, and "
          "*not* a source of secrets: a key or a token comes from `bytes`, "
          "which is the operating system's own entropy."),
        T(["Name", "Signature", "Does"],
          [["`Random`", "`struct`", "256 bits of state; a copy is a second "
            "generator producing the same numbers"],
           ["`seeded`", "`(seed: u64) -> Random`", "reproducible"],
           ["`new`", "`() -> Random`", "seeded from the OS"],
           ["`Random::next`", "`(&var self) -> u64`", "the next 64 bits"],
           ["`Random::below`", "`(&var self, bound: i64) -> i64`",
            "`0` up to but not including `bound`, unbiased"],
           ["`Random::between`", "`(&var self, low: i64, high: i64) -> i64`",
            "both ends included"],
           ["`Random::float` / `floatBetween`", "`(&var self) -> f64`",
            "`[0, 1)` with 53 bits; or `[low, high)`"],
           ["`Random::chance` / `coin`", "`(&var self, p: f64) -> bool`",
            "true with probability `p`; or evenly"],
           ["`Random::choose` / `pick`", "`<T>(&var self, [T]) -> T?`",
            "one element of a slice, or a copy of one from a borrowed `Vector`"],
           ["`Random::shuffle`", "`<T>(&var self, &var Vector<T>)`",
            "a uniformly random order, in place"],
           ["`bytes`", "`(count: i64) -> Vector<u8>`",
            "straight from the OS — for anything secret"],
           ["`next`, `below`, `between`, `float`, `chance`, `coin`, `choose`, `shuffle`",
            "free functions",
            "the same, on one shared generator seeded from the OS on first use"]]),
        S("""import std::io
import std::random
import std::collections::vector

fn main() -> i64 {
    var fixed = random::seeded(42)
    io::println(fixed.next())                 // always this number
    io::println(fixed.between(1, 6) >= 1)

    var rng = random::new()                   // different every run
    var deck = vec!("A", "K", "Q", "J")
    rng.shuffle(&var deck)
    io::println(deck.length())
    io::println(rng.float() < 1.0)
    io::println(random::bytes(16).length())
    0
}""", mode="run", title="Repeatable, and not"),

        H("std::hash"),
        P("Hashes with a name, for when the structural `mem::hash` is not "
          "what is wanted: a hash that is written to a file, sent over a "
          "wire, or compared with one someone else computed has to be a "
          "*particular* hash. FNV-1a and CRC-32 are for tables and "
          "checksums; SHA-256 is the one to use where a collision would "
          "matter. Each is a function over a `String` or a `[u8]`, and a "
          "struct that can be fed a piece at a time."),
        T(["Name", "Signature", "Does"],
          [["`fnv1a64` / `fnv1a64Bytes`", "`(String) -> u64` / `([u8]) -> u64`",
            "FNV-1a, 64 bits"],
           ["`fnv1a32`", "`(String) -> u32`", "FNV-1a, 32 bits"],
           ["`crc32` / `crc32Bytes`", "`(String) -> u32` / `([u8]) -> u32`",
            "CRC-32 as zip and PNG use it"],
           ["`sha256`", "`(String) -> String`", "the digest as 64 hex characters"],
           ["`sha256Bytes`", "`([u8]) -> [32:u8]`", "the digest itself"],
           ["`Fnv64`, `Fnv32`, `Crc32`, `Sha256`", "`struct`",
            "incremental: `feed(u8)`, `feedBytes([u8])`, `feedText(String)`, "
            "then `finish()`"],
           ["`hex` / `hex64` / `hex32`", "`([u8]) -> String` and friends",
            "lowercase hexadecimal"]]),
        S("""import std::io
import std::hash

fn main() -> i64 {
    io::println(hash::hex64(hash::fnv1a64("hello")))
    io::println(hash::hex32(hash::crc32("hello")))
    io::println(hash::sha256("hello"))

    var h = hash::Sha256 {}              // the same digest, fed in pieces
    h.feedText("hel")
    h.feedText("lo")
    io::println(h.finishHex() == hash::sha256("hello"))
    0
}""", mode="run", title="Named hashes"),

        H("std::json"),
        P("Reading and writing JSON. A document is a `json::Value` — one of "
          "seven shapes: the six the grammar has, plus a whole number kept "
          "apart from a fractional one so that `3` survives a round trip. "
          "Every way of looking inside answers with an `Option`, because a "
          "document is data from somewhere else; `[]` answers with a `Value` "
          "instead, and reaching into what is not there gives `null`, so a "
          "chain of subscripts ends rather than aborting halfway."),
        T(["Name", "Signature", "Does"],
          [["`parse`", "`(String) -> Result<Value, Error>`",
            "the whole text as one value; anything trailing is an error"],
           ["`write` / `pretty`", "`(Value) -> String`",
            "compact, or laid out one member per line"],
           ["`load` / `save`", "`(path) -> Result<Value, Error>` / `(path, Value) -> Error?`",
            "the same, over a file"],
           ["`Value`", "`enum`",
            "`Null`, `Bool`, `Int`, `Number`, `Text`, `Array`, `Object`"],
           ["`Value::asBool` … `asObject`", "`(&self) -> T?`",
            "what is inside, if it is that; `asInt` does not round"],
           ["`Value::get` / `index`", "`(&self, String) -> Value?` / `(&self, i64) -> Value?`",
            "a member, or an element"],
           ["`value[\"name\"]` / `value[0]`", "`-> Value`",
            "the same, with `Null` for what is not there"],
           ["`Value::kind` / `length` / `isNull`", "", "asking about the shape"],
           ["`Value::withMember` / `withItem`", "`(&self, …) -> Value`",
            "building, chainably — what `json!` expands to"],
           ["`object` / `array` / `null`", "`() -> Value`", "an empty one"],
           ["`Object`", "`class`",
            "members in insertion order: `at`, `put`, `remove`, `keys`, `holds`"],
           ["`Array`", "`class`", "`at`, `push`, `length`"],
           ["`Error`", "`struct { kind, line, column, offset, detail }`",
            "what went wrong, and where; prints as a sentence"],
           ["`json!`", "`macro`",
            "a document written the way JSON is written"],
           ["`v into json::Value`", "",
            "`i64`, `i32`, `f64`, `bool` and `String` convert"]]),
        S("""import std::io
import std::json

fn main() -> i64 {
    match json::parse("{\\"name\\": \\"ada\\", \\"tags\\": [\\"a\\", \\"b\\"], \\"age\\": 36}") {
        Ok(doc) => {
            io::println(doc["name"])
            io::println(doc["tags"][1])
            io::println(doc["missing"]["deeper"])       // null, not an abort
            io::println(doc.get("age").unwrap().asInt() ?? 0)
            io::println(json::write(doc))
        },
        Err(e) => io::println(e),
    }
    let year = 2025
    let built = json!{ "name": "ada", "next": year + 1, "tags": ["x", null] }
    io::println(built)
    match json::parse("[1, 2,]") {
        Ok(v) => io::println(v),
        Err(e) => io::println(e),                      // says where
    }
    0
}""", mode="run", title="Parsing, reaching in, writing out"),
        N("`json!{ ... }` uses the invocation's own braces as the object's. "
          "Inside, `{ }` is an object, `[ ]` an array, `null` is null, and "
          "anything else is an expression converted with `into`. "
          "`json![1, 2]` is an array at the top level and `json!(42)` a "
          "value on its own.",
          label="Writing a document"),

        H("std::cli"),
        P("The command line, read the way the user wrote it. Say what the "
          "program takes; ask what it was given. `--help` prints a usage page "
          "built from the descriptions, and a mistake — an unknown option, a "
          "missing value, a positional that was not given — is named and, "
          "with `parseOrExit`, exits 2."),
        T(["Name", "Signature", "Does"],
          [["`Parser`", "`class`", "`Parser(program, summary)`"],
           ["`Parser::flag`", "`(&var self, name, short, help)`",
            "`--name` / `-n`, present or not"],
           ["`Parser::option`", "`(&var self, name, short, help, fallback)`",
            "takes a value; an empty fallback means no default"],
           ["`Parser::optionMany`", "`(&var self, name, short, help)`",
            "repeatable; every value kept"],
           ["`Parser::positional`", "`(&var self, name, help)`",
            "required, in declaration order"],
           ["`Parser::rest`", "`(&var self, name, help)`",
            "any number of further positionals"],
           ["`Parser::setVersion`", "`(&var self, version)`",
            "makes `--version` an option"],
           ["`Parser::parse`", "`(&self) -> Result<Arguments, Error>`",
            "the program's own command line"],
           ["`Parser::parseList`", "`(&self, Vector<String>) -> Result<Arguments, Error>`",
            "a list of one's own — for tests"],
           ["`Parser::parseOrExit`", "`(&self) -> Arguments`",
            "prints help or the mistake, and exits"],
           ["`Parser::usage` / `usageLine`", "`(&self) -> String`",
            "the help page, or its first line"],
           ["`Arguments::has`", "`(&self, name) -> bool`", "was the flag given"],
           ["`Arguments::value` / `intValue`", "`(&self, name) -> String?` / `i64?`",
            "the option's value, or its default"],
           ["`Arguments::values`", "`(&self, name) -> Vector<String>`",
            "everything a repeatable option was given"],
           ["`Arguments::positional`", "`(&self, index) -> String?`",
            "a declared positional"],
           ["`Arguments::rest`", "`(&self) -> Vector<String>`",
            "whatever followed the declared ones"],
           ["`Error`", "`enum`",
            "`Help`, `Version`, `UnknownOption`, `MissingValue`, "
            "`FlagGivenValue`, `MissingPositional`, `TooManyPositionals`"]]),
        S("""import std::io
import std::cli
import std::collections::vector

fn main() -> i64 {
    var app = cli::Parser("greet", "Prints a greeting.")
    app.flag("loud", "l", "shout it")
    app.option("name", "n", "who to greet", "world")
    app.optionMany("extra", "x", "more names")
    app.positional("times", "how many times")
    app.rest("files", "files to read afterwards")

    // `parse()` would read the real command line; a list stands in here.
    match app.parseList(vec!("-l", "--name=ada", "-x", "p", "-xq", "3", "a.txt")) {
        Ok(args) => {
            io::println(args.has("loud"))
            io::println(args.value("name") ?? "")
            io::println(args.values("extra").length())
            io::println(args.intValue("times") ?? 0)
            io::println(args.rest().length())
        },
        Err(e) => io::println(e),
    }
    match app.parseList(vec!("--bogus")) {
        Ok(args) => io::println("accepted?"),
        Err(e) => io::println(e),
    }
    io::print(app.usage())
    0
}""", mode="run", title="Declaring, parsing, and the help page"),
        T(["Written", "Read as"],
          [["`--name value`, `--name=value`", "the option and its value"],
           ["`-n value`, `-nvalue`", "the same, by short name"],
           ["`-lv`", "two flags"],
           ["`--`", "everything after it is positional, however it looks"],
           ["`-`", "a positional — standard input, by convention"],
           ["`--help`, `-h`, `--version`", "handled for you"]],
          caption="The spellings accepted"),

        H("std::mem"),
        P("Layout questions, an allocator, and a smart pointer. Most programs "
          "need only the first and the last."),
        T(["Member", "Signature", "Does"],
          [["`size_of`", "`<T>() -> usize`", "bytes one `T` occupies"],
           ["`align_of`", "`<T>() -> usize`", "the alignment `T` requires"],
           ["`is_counted`", "`<T>() -> bool`", "whether `T` is reference "
            "counted — decided at compile time"],
           ["`hash`", "`<T>(value: T) -> u64`", "a structural hash, worked out "
            "from the layout; no bound on `T`"],
           ["`equals`", "`<T>(a: T, b: T) -> bool`", "structural equality, "
            "consistent with `hash`"],
           ["`Handle<T>`", "`class`", "one `T` on the heap, reference counted; "
            "`*h` reads it, `*h = v` writes it, `look()`/`touch()` borrow it"],
           ["`of`", "`<T>(value: T) -> Handle<T>`", "a handle, type inferred"],
           ["`Box<T>`", "`class`", "one `T` on the heap with a single owner "
            "— the same reach-through, no sharing"],
           ["`boxed`", "`<T>(value: T) -> Box<T>`", "a box, type inferred"],
           ["`Rc<T>` / `Weak<T>`", "`class`", "a counted value and a "
            "reference that does not keep it alive; `w.get()` answers "
            "`Rc<T>?`"],
           ["`shared`", "`<T>(value: T) -> Rc<T>`", "an `Rc`, type inferred"],
           ["`replace`", "`<T>(place: &var T, value: T) -> T`", "puts `value` "
            "there and hands back what was there"],
           ["`take`", "`<T>(place: &var T) -> T`", "the same, leaving the "
            "type's default behind"],
           ["`store`", "`<T>(place: &var T, value: T)`", "writes, destroying "
            "what was there"],
           ["`Allocator`", "`mark`", "`allocate`, `deallocate`, `reallocate`"],
           ["`allocator`", "`SystemAllocator`", "the process heap"],
           ["`noBlock`", "`() -> *var u8`", "a block pointer to nothing"],
           ["`isNull`", "`(block: *var u8) -> bool`", "tests one"],
           ["`copy`", "`(dst: *var u8, src: *u8, bytes: usize)`", "moves bytes"],
           ["`retain` / `release`", "`<T>(value: T)`", "**unsafe** — counting "
            "by hand, for storage the compiler cannot see"],
           ["`slice_of`", "`<T>(block: *var T, count: usize) -> [T]`", "**unsafe** — "
            "`count` elements at `block` as a slice; no copy, and the block's "
            "extent is the caller's promise"],
           ["`slice_data`", "`<T>(values: [T]) -> *var T`", "**unsafe** — "
            "the other direction: where a slice's elements actually are, so "
            "they can be moved out one by one rather than copied"]]),
        S("""import std::io
import std::mem

struct Pixel { r: u8, g: u8, b: u8, a: u8 }

fn main() -> i64 {
    io::println(mem::size_of<i64>().$str())
    io::println(mem::align_of<i64>().$str())
    io::println(mem::size_of<Pixel>().$str())
    0
}""", mode="run", title="What a type costs"),
        P("`Handle<T>` is the one to reach for. It owns a value on the heap "
          "and is itself a class, so it is reference counted: copies share the "
          "value and the last one to go releases it. Nothing about using one "
          "is unsafe — there is no way to reach the value afterwards, and no "
          "way to free it twice."),
        S("""import std::io
import std::mem

fn main() -> i64 {
    var counter = mem::Handle<i64>(41)
    counter.set(counter.get() + 1)
    io::println(counter.get().$str())

    // Or through `*`, which a handle overloads.
    *counter += 8
    io::println((*counter).$str())

    // Two names, one value.
    let shared = mem::of("first")
    let alias = shared
    alias.set("second")
    io::println(shared.get())
    0
}""", mode="run", title="A handle"),
        P("`look` and `touch` are how a structure built out of handles is "
          "walked. `*h` and `get()` both hand back a **copy** of what the "
          "handle owns — under single ownership that means cloning everything "
          "below it, which for a list is the rest of the list, at every step. "
          "A borrow reads the one that is there."),
        S('''import std::io
import std::mem
import std::mem::{Handle}

enum Link<T> { Empty, More(Handle<Node<T>>) }
struct Node<T> { elem: T, next: Link<T> }
struct List<T> { head: Link<T> }

extend List {
    fn push(&var self, value: T) {
        self.head = Link::More(Handle<Node<T>>(Node<T> {
            elem: value, next: mem::replace(&var self.head, Link::Empty)
        }))
    }

    /// A walk that reads: one borrow at a time, nothing copied.
    fn length(&self) -> i64 {
        var n = 0
        var cur = &self.head
        while cur is Link::More(node) { n += 1; cur = &node.look().next }
        n
    }

    /// A walk that writes: the cursor is a `&var` the whole way.
    fn doubleAll(&var self) {
        var cur = &var self.head
        loop {
            match cur {
                Link::Empty => break,
                Link::More(node) => {
                    node.touch().elem = node.look().elem * 2
                    cur = &var node.touch().next
                }
            }
        }
    }
}

fn main() -> i64 {
    var list = List<i64> { head: Link::Empty }
    list.push(1)
    list.push(2)
    io::println("length " + list.length().$str())
    list.doubleAll()
    io::println("head " + (if list.head is Link::More(n) { n.look().elem } else { 0 }).$str())
    0
}''', mode="run", title="Walking a list of handles"),
        T(["`Handle<T>`", "Hands back"],
          [["`get()`", "a copy of the value — a share under counting, a clone "
            "under single ownership"],
           ["`*h`", "the same, as an operator"],
           ["`look()`", "`&T from self` — the value where it lies"],
           ["`touch()`", "`&var T from self` — the same, to write through"],
           ["`set(v)`", "replaces the value"]]),
        N("However long the structure is, giving it back costs no stack: a "
          "destruction reached from inside another one is queued and run "
          "after it, so a list of a million links is freed in a loop rather "
          "than a million nested calls.", label="Long chains"),
        P("The allocator underneath is a mark, so a program can supply its "
          "own. Blocks come back untyped and uninitialised, every one must go "
          "back exactly once, and reading through the pointer is unsafe — "
          "which is why almost nothing should use it directly."),
        S("""import std::io
import std::mem

@safe("the block is sized for four i64 and no index goes past four")
fn main() -> i64 {
    let block = mem::allocator.allocate(4 as usize * mem::size_of<i64>())
    if mem::isNull(block) { return 1 }

    let cells = unsafe { block as *var i64 }
    var i = 0
    while i < 4 {
        unsafe { cells[i] = (i + 1) * 100 }
        i += 1
    }
    var total = 0
    i = 0
    while i < 4 {
        total += unsafe { cells[i] }
        i += 1
    }
    io::println(total.$str())

    mem::allocator.deallocate(block)
    0
}""", mode="run", title="Using the allocator directly"),
        N("`p[n]` on a raw pointer is offset arithmetic with nothing to check "
          "against — the one indexing form that is never bounds checked. It "
          "needs an unsafe context, and `*var T` to be written through.",
          label="Raw indexing", tone="warn"),

        H("std::mem: Buffer"),
        P("A fixed-size run of values on the heap, checked on every access. An "
          "array's length is part of its type, so it cannot be decided at run "
          "time; a `Buffer` can. Reading and writing both go through `[]`."),
        T(["Member", "Signature", "Does"],
          [["`Buffer<T>`", "`(size: i64, fill: T)`", "`size` slots, each "
            "holding `fill`"],
           ["`length`", "`(&self) -> i64`", "how many, fixed for its life"],
           ["`isEmpty`", "`(&self) -> bool`", ""],
           ["`holds`", "`(&self, index: i64) -> bool`", "is that a slot?"],
           ["`at`", "`(&self, index: i64) -> T?`", "the value, or nothing — "
            "a slot of all zero bytes reads as nothing"],
           ["`read`", "`(&self, index: i64) -> T?`", "the value, whatever its "
            "bytes are; only the index is checked"],
           ["`get`", "`(&self, index: i64) -> T`", "the value; **aborts** if "
            "absent"],
           ["`put`", "`(&var self, index: i64, value: T) -> bool`", "writes; "
            "false if out of range"],
           ["`fill`", "`(&var self, value: T)`", "writes every slot"],
           ["`b[i]` / `b[i] = v`", "—", "`get` and `put`, both bounds "
            "checked"]]),
        N("`at` decides \"nothing has been written here\" from the slot's "
          "bytes being all zero, which is right for a slot nobody has touched "
          "and wrong for one holding a value whose bytes happen to all be "
          "zero — an empty `String`, or the first variant of a counted enum "
          "that carries nothing. `read` asks only whether the index is in "
          "range, and is the form for a caller that keeps its own record of "
          "which slots are live.",
          label="Zero is a value", tone="warn"),
        S("""import std::io
import std::mem

fn main() -> i64 {
    var b = mem::Buffer<i64>(size: 4, fill: 0)
    b[0] = 10
    b[1] = 20
    b[2] = b[0] + b[1]
    b[3] += 5
    io::println(b[0].$str() + " " + b[1].$str() + " " +
                b[2].$str() + " " + b[3].$str())

    // An index from outside is a question, not an assumption.
    io::println(b.at(9).hasValue().$str())
    io::println(b.holds(3).$str())
    0
}""", mode="run", title="Reading and writing through `[]`"),
        S("""import std::io
import std::mem

fn main() -> i64 {
    var b = mem::Buffer<i64>(size: 2, fill: 0)
    io::println("about to read past the end")
    io::println(b[5].$str())
    0
}""", mode="panic", title="Past the end aborts"),
        N("The `fill` is required rather than optional. A buffer with "
          "uninitialised slots would hand out whatever the allocator left "
          "there — the one thing a safe container must not do — so every slot "
          "holds a real value from the moment it exists. For a "
          "reference-counted `T` the buffer owns one reference per slot and "
          "gives them all back.", label="Why a fill"),

        H("std::dictionary"),
        P("Keyed lookup: a `Map` from keys to values, and a `Set` of keys "
          "alone. Both are open-addressed hash tables with linear probing, "
          "backed by three `mem::Buffer`s — one for keys, one for values, and "
          "one for the state of each slot. They double when they pass two "
          "thirds full."),
        S('import std::io\nimport std::dictionary\n\nfn main() -> i64 {\n    var ages = dictionary::Map<String, i64>()\n    ages.put("ada", 36)\n    ages.put("grace", 45)\n\n    match ages.at("ada") {\n        Some(n) => io::println("ada is " + n.$str()),\n        None    => io::println("no ada"),\n    }\n    io::println("holds grace: " + ages.holds("grace").$str())\n    io::println("unknown: " + ages.atOr("nobody", -1).$str())\n\n    let seen = dictionary::setOf(["a", "b", "a", "c"])\n    io::println("distinct: " + seen.length().$str())\n    0\n}', mode="run", title="A map and a set"),
        N("A key is hashed and compared **structurally**, by `mem::hash` and "
          "`mem::equals`. The compiler works both out from the layout, so a "
          "key needs nothing of its own: no `Hashable` mark to bind, and no "
          "`Display` standing in for one. Any type at all can be a key — a "
          "struct, an enum with payloads, a tuple, a class.",
          label="What makes two keys the same"),
        T(["Key type", "Two keys are the same when"],
          [["integers, floats, `bool`, `Character`", "the values are equal"],
           ["`String`, `CString`", "the **contents** match, not the address"],
           ["a class", "they are the **same object** — two objects with equal "
            "fields are two keys"],
           ["struct, tuple, array", "every part matches, part by part"],
           ["an enum", "the variant matches, and its payload does"]]),
        T(["Map method", "Signature", "Does"],
          [["`length` / `isEmpty`", "`(&self) -> ...`", ""],
           ["`at`", "`(&self, key: K) -> V?`", "the value, or nothing"],
           ["`holds`", "`(&self, key: K) -> bool`", ""],
           ["`atOr`", "`(&self, key: K, fallback: V) -> V`", "the value, or "
            "`fallback`"],
           ["`put`", "`(&var self, key: K, value: V) -> V?`", "stores; returns "
            "what it replaced"],
           ["`remove`", "`(&var self, key: K) -> V?`", "returns what it held"],
           ["`clear`", "`(&var self)`", "drops everything, keeps the storage"],
           ["`keysOf` / `valuesOf`", "`(&self) -> Vector<...>`", "everything, "
            "in table order"]]),
        T(["Set method", "Signature", "Does"],
          [["`length` / `isEmpty`", "`(&self) -> ...`", ""],
           ["`holds`", "`(&self, value: T) -> bool`", ""],
           ["`add`", "`(&var self, value: T) -> bool`", "false when already "
            "there"],
           ["`remove`", "`(&var self, value: T) -> bool`", "false when it was "
            "not"],
           ["`membersOf`", "`(&self) -> Vector<T>`", "everything, in table "
            "order"]]),
        T(["Written", "Means", "When it is not there"],
          [["`m[key]`", "the value", "**aborts** — `at` answers instead"],
           ["`m[key] = v`", "stores it", "—"],
           ["`m[key] += v`", "reads, adds, stores", "aborts on the read"],
           ["`s[value]`", "whether it is a member", "`false`; a Set has "
            "nothing to store, so there is no `s[v] = ...`"]]),
        P("Both iterate. A `Map` produces `(key, value)` pairs and a `Set` "
          "produces its members, so the `std::iter` adaptors work on either:"),
        S("""import std::io
import std::dictionary

fn main() -> i64 {
    var ages = dictionary::Map<String, i64>()
    ages["ada"] = 36
    ages["grace"] = 45
    ages["ada"] += 1

    var total = 0
    for (name, age) in ages { total += age }
    io::println("total " + total.$str())

    let seen = dictionary::setOf(["a", "b", "c"])
    io::println("holds b: " + seen["b"].$str())
    0
}""", mode="run", title="Subscripting and walking"),
        N("`keysOf`, `valuesOf` and `membersOf` hand back whatever order the "
          "table happens to hold — not insertion order, and not stable across "
          "a resize. Sort the result when the order matters.",
          label="Order is not promised", tone="warn"),
        N("A removed slot becomes a tombstone rather than an empty one. "
          "Emptying it would cut the probe chain for any key that walked past "
          "it on its way in, and those keys would silently stop being found.",
          label="Why removal leaves a mark"),
        P("`Set<T>` is a `Map<T, bool>` whose values are all true, which is "
          "what a set is — so there is one probing implementation rather than "
          "two. `dictionary::setOf(values)` and `dictionary::mapOf(keys, "
          "values)` build one from a slice."),
        P("A map is common enough to be worth writing short. `[K:V]` is the "
          "type and `[key: value, ...]` is the value, with `[:]` for the "
          "empty one:"),
        S("""import std::io

fn count(m: [String:i64]) -> i64 { m.length() }

fn main() -> i64 {
    let ages: [String:i64] = ["ada": 36, "bob": 41]
    io::println(ages.at("ada").or(0))
    io::println(count(ages))

    // Pairs may go on their own lines.
    let words = [
        "one": 1,
        "two": 2,
    ]
    io::println(words.at("two").or(0))

    // `[:]` takes its types from where it is going.
    let empty: [String:i64] = [:]
    io::println(empty.length())
    0
}""", mode="run", title="Written short"),
        N("`[3:i64]` is an array of three and `[String:i64]` is a map: an "
          "array's length is a **number** and a map's key is a **type**, so "
          "what was meant is decided by what the name means rather than by "
          "the punctuation. `[SIZE:i64]` with `SIZE` a constant is still an "
          "array.", label="How it is told from an array"),

        H("std::collections"),
        P("Three ways to hold a run of values, and one module each. An "
          "**array** `[5:i64]` has its length in its type. A **slice** "
          "`[i64]` carries a length beside a pointer, and an array converts to "
          "one on its own. A **`Vector<T>`** owns its storage and grows."),
        T(["Holding", "Written", "Length", "Owns its storage"],
          [["Array", "`[5:i64]`", "part of the type, constant", "yes, inline"],
           ["Slice", "`[i64]`", "carried at run time", "no — it points at "
            "someone else's"],
           ["Vector", "`vector::Vector<i64>`", "grows as you push", "yes, on "
            "the heap"]]),

        H("std::collections::slice"),
        P("Everything you can do with a run of values you did not allocate. "
          "Every function takes a slice, so an array works too. The compiler "
          "provides `$length` and `$isEmpty`, and `values[a..b]` slices one; "
          "the rest is here."),
        T(["Function", "Signature", "Does"],
          [["`at`", "`([T], i64) -> T?`", "checked access — nothing when out "
            "of range"],
           ["`first` / `last`", "`([T]) -> T?`", ""],
           ["`indexWhere`", "`([T], @function(T) -> bool) -> i64?`", "index of "
            "the first match"],
           ["`firstWhere`", "`([T], @function(T) -> bool) -> T?`", "the first "
            "match itself"],
           ["`anyOf` / `allOf`", "`([T], @function(T) -> bool) -> bool`", "one "
            "matches / every one does"],
           ["`countWhere`", "`([T], @function(T) -> bool) -> i64`", "how many "
            "match"],
           ["`indexOf` / `contains`", "`<T: Display>([T], T) -> ...`", "search "
            "by value, compared as it prints"],
           ["`minimumBy` / `maximumBy`", "`([T], @function(T, T) -> bool) -> T?`",
            "extremes by your ordering"],
           ["`fold`", "`([T], A, @function(A, T) -> A) -> A`", "combine into "
            "one, left to right"],
           ["`toVector` / `reversed` / `filtered`", "`([T], ...) -> Vector<T>`",
            "these allocate, which is why they say `Vector`"],
           ["`sortedBy`", "`([T], @function(T, T) -> bool) -> Vector<T>`",
            "stable insertion sort, original untouched"],
           ["`join`", "`<T: Display>([T], String) -> String`", "rendered and "
            "separated"],
           ["`iterate`", "`([T]) -> SliceIter<T>`", "a cursor, so the "
            "`std::iter` adaptors chain"]]),
        N("A slice indexes natively — `values[i]`, bounds checked — and `for "
          "value in values` needs nothing either. `iterate` exists only to "
          "reach `map`, `filter` and `zip`, which arrive through the "
          "`Iterator` mark: a mark cannot be bound to a builtin type, so the "
          "cursor is what carries them.",
          label="Why a slice needs `iterate` and a Vector does not"),
        N("The predicate forms exist because two values of an arbitrary `T` "
          "cannot be compared without knowing something about `T`. The "
          "`Display` forms compare values *as they print*, which is exact for "
          "every builtin and needs only a `Display` binding for anything else "
          "— there is no separate equality bound to satisfy.",
          label="Why a predicate, and why `Display`"),
        S("""import std::io
import std::collections::slice

fn main() -> i64 {
    let scores: [4:i64] = [3, 1, 4, 1]

    io::println(slice::join(scores, "-"))
    io::println(slice::contains(scores, 4).$str())
    io::println(slice::fold(scores, 0, ||(a: i64, n: i64) -> i64 { a + n }).$str())

    let ordered = slice::sortedBy(scores, ||(a: i64, b: i64) -> bool { a < b })
    var out = ""
    for n in ordered { out += n.$str() + " " }
    io::println(out)
    0
}""", mode="run", title="Arrays and slices"),

        H("std::collections::vector"),
        P("A growable array, and the worked example for everything above: "
          "every unsafe operation in the standard library's `Vector` is inside "
          "it, behind an interface that hands back `Option` where an index "
          "might not exist."),
        T(["Method", "Signature", "Does"],
          [["`length`", "`(&self) -> i64`", "how many it holds"],
           ["`isEmpty`", "`(&self) -> bool`", ""],
           ["`capacityOf`", "`(&self) -> i64`", "room before it must grow"],
           ["`at`", "`(&self, index: i64) -> T?`", "the element, or nothing"],
           ["`get`", "`(&self, index: i64) -> T`", "the element; **aborts** if "
            "absent"],
           ["`last`", "`(&self) -> T?`", "the final element"],
           ["`push`", "`(&var self, value: T)`", "appends, growing if needed"],
           ["`pop`", "`(&var self) -> T?`", "removes and returns the last"],
           ["`set`", "`(&var self, index: i64, value: T) -> bool`", "replaces; "
            "false if out of range"],
           ["`clear`", "`(&var self)`", "drops every element, keeps the "
            "storage"],
           ["`reserve`", "`(&var self, wanted: i64)`", "room for `wanted`"],
           ["`asSlice`", "`(&self) -> [T]`", "every element as a slice over "
            "the vector's own storage — no copy"],
           ["`v[i]` / `v[i] = x`", "", "the direct forms — **abort** out of "
            "range, where `at` and `set` answer"],
           ["`from`", "`<T>(values: [T]) -> Vector<T>`", "builds one from an "
            "array or slice — every element is copied, so `T` must be "
            "copyable"],
           ["`drain`", "`<T>(values: [T]) -> Vector<T>`", "the same, by "
            "**moving** each element out of `values`; this is what `vec!` "
            "expands to, and it is why a `vec!` of owning values works"]]),
        S("""import std::io
import std::collections::vector

fn main() -> i64 {
    var squares = vector::Vector<i64>()
    var i = 1
    while i <= 6 {
        squares.push(i * i)
        i += 1
    }
    io::println("length   " + squares.length().$str())
    io::println("capacity " + squares.capacityOf().$str())
    io::println("at 2     " + squares.get(2).$str())
    io::println("popped   " + squares.pop().unwrap().$str())

    // Out of range is an Option, not a crash.
    match squares.at(99) {
        Some(v) => io::println("at 99 " + v.$str()),
        None => io::println("at 99 nothing there"),
    }
    0
}""", mode="run", title="A vector of numbers"),
        P("A vector of reference-counted values keeps the books itself: it "
          "claims a reference when a value goes in and gives one up when it "
          "comes out or when the vector is released. The count at the end is "
          "the proof."),
        S("""import std::io
import std::process
import std::collections::vector

fn main() -> i64 {
    let before = process::liveObjectCount()
    {
        var names = vector::from(["ada", "grace", "alan"])
        names.push("edsger")
        io::println(names.length().$str() + ", last " + names.last().unwrap())
        io::println("popped " + names.pop().unwrap())
    }
    let after = process::liveObjectCount()
    io::println("balanced " + (before == after).$str())
    0
}""", mode="run", title="A vector of strings"),
        N("`Vector` is a class, so passing one around shares it rather than "
          "copying. Its storage is a single block from `mem::allocator`, grown "
          "by doubling and handed back when the last reference goes.",
          label="Sharing"),
        P("`asSlice` views the elements as a `[T]` without copying them, so "
          "anything written against a slice — `std::collections::slice`, a "
          "slice pattern, a function of your own — reads a vector as it is. "
          "The slice is a view, not an owner: it is valid while the vector "
          "lives and until the next `push`, `reserve` or `clear`, which may "
          "move the storage. Underneath is `mem::slice_of<T>(block, count)`, "
          "the one intrinsic that makes a slice from an address and a count, "
          "for a container of your own to do the same."),
        S("""import std::io
import std::collections::vector
import std::collections::slice

fn total(values: [i64]) -> i64 {
    var t = 0
    for v in values { t += v }
    t
}

fn main() -> i64 {
    let v = vec!(3, 4, 5)
    let view = v.asSlice()
    io::println(total(view).$str())
    io::println((slice::last(view) ?? 0).$str())
    match view {
        [first, .., last] => io::println((first + last).$str()),
        _ => io::println("short"),
    }
    v.push(6)                      // may move the storage: take a new view
    io::println(v.asSlice().$length().$str())
    0
}""", mode="run", title="A vector as a slice"),

        H("Writing an allocator"),
        P("`Allocator` is a mark, so a program can supply its own — an arena "
          "that frees everything at once, a pool of fixed-size blocks, or a "
          "counting wrapper around the system heap. Implement three methods "
          "and anything that takes an allocator will use it."),
        S("""import std::io
import std::mem

/// A bump allocator: hands out slices of one block and frees nothing until
/// the whole arena goes. Fast, and exactly right for a batch of short-lived
/// values that die together.
pub class Arena {
    block: *var u8
    size: usize
    used: usize
    handed: i64

    fn init(self, size: usize) {
        self.block = mem::allocator.allocate(size)
        self.size = size
        self.used = 0
        self.handed = 0
    }

    fn deinit(self) { mem::allocator.deallocate(self.block) }

    pub fn handedOut(&self) -> i64 { self.handed }
    pub fn usedBytes(&self) -> i64 { self.used as i64 }
}

bind mem::Allocator to Arena {
    /// Bumps the cursor. Returns null when the arena is full, which is what
    /// `mem::isNull` is for.
    @safe("the cursor never passes the size checked on the line above")
    fn allocate(&self, bytes: usize) -> *var u8 {
        // Keep every block 8-aligned, as the system allocator would.
        let need = (bytes + 7) / 8 * 8
        if self.used + need > self.size { return mem::noBlock() }
        let at = self.used
        self.used += need
        self.handed += 1
        unsafe { (self.block as u64 + at as u64) as *var u8 }
    }

    /// An arena frees in one go, so a single block going back is a no-op.
    fn deallocate(&self, block: *var u8) {}

    /// Growing in place is not something a bump allocator can do; hand back a
    /// fresh block and let the caller copy.
    fn reallocate(&self, block: *var u8, bytes: usize) -> *var u8 {
        self.allocate(bytes)
    }
}

@safe("every block below is sized and written through its own pointer")
fn main() -> i64 {
    let arena = Arena(1024 as usize)

    // Three blocks of four i64 each, straight out of the arena.
    var round = 0
    while round < 3 {
        let block = arena.allocate(4 as usize * mem::size_of<i64>())
        if mem::isNull(block) { break }
        let cells = unsafe { block as *var i64 }
        var i = 0
        while i < 4 {
            unsafe { cells[i] = (round + 1) * (i + 1) }
            i += 1
        }
        io::println("block " + round.$str() + " ends with " +
                    unsafe { cells[3] }.$str())
        round += 1
    }

    io::println("handed out " + arena.handedOut().$str() + " blocks, " +
                arena.usedBytes().$str() + " bytes")
    0
}""", mode="run", title="A bump allocator"),
        N("The three methods are the whole contract: `allocate` returns a "
          "block or null, `deallocate` takes one back, and `reallocate` "
          "resizes. Nothing else in the language needs to know which allocator "
          "it is talking to.", label="Three methods"),

        H("Built-in methods and the `$` sigil"),
        P("These are not library functions — the compiler knows them, and they "
          "answer questions about a value rather than doing anything a library "
          "could. They are written with a `$`, which is what keeps them out of "
          "the way of the methods your own types declare: a type is free to "
          "have its own `length` or `str`, and neither name can ever shadow "
          "the other."),
        T(["Receiver", "Method", "Result"],
          [["`String`", "`$length()` / `$len()`", "`i64` — bytes"],
           ["`String`", "`$charCount()`", "`i64` — characters"],
           ["`String`", "`$isEmpty()`", "`bool`"],
           ["`String`", "`$at(i)`", "`Character`"],
           ["`String`", "`$byteAt(i)`", "`u8`"],
           ["`String`", "`$substring(from, to)`", "`String`"],
           ["`String`", "`$find(needle)`", "`i64`, `-1` when absent"],
           ["`String`", "`$repeat(n)`", "`String`"],
           ["`String`", "`$toInt()`", "`Option<i64>`"],
           ["`String`", "`$toFloat()`", "`Option<f64>`"],
           ["`String`", "`$cstr()`", "`CString` for C"],
           ["`String`", "`$hash()`", "`u64`"],
           ["`[N:T]`, `[T]`", "`$length()` / `$len()`", "`i64`"],
           ["`[N:T]`, `[T]`", "`$isEmpty()`", "`bool`"],
           ["any scalar", "`$str()`", "`String`"]]),
        S("""import std::io

fn main() -> i64 {
    let text = "hello, world"
    io::println(text.$length().$str())
    io::println(text.$substring(7, 12))
    io::println(text.$find("world").$str())
    io::println("-".$repeat(12))
    io::println(text.$at(0).$str())
    io::println("120".$toInt().or(0).$str())
    io::println("not a number".$toInt().hasValue().$str())
    0
}""", mode="run", title="String methods"),
        P("Because the two namespaces are separate, a type can answer to both "
          "spellings without either shadowing the other."),
        S("""import std::io

struct Tag { text: String }

extend Tag {
    // The same names the compiler uses, and no conflict.
    pub fn length(&self) -> String { "a tag" }
    pub fn str(&self) -> i64 { self.text.$length() }
}

fn main() -> i64 {
    let t = Tag { text: "hello" }
    io::println(t.length())            // the type's own
    io::println(t.str().$str())        // the type's own, then the compiler's
    io::println(t.text.$length().$str())
    0
}""", mode="run", title="Both namespaces at once"),
        S("""fn main() -> i64 {
    let values: [3:i64] = [1, 2, 3]
    values.length()
}""", mode="diag", title="Forgetting the sigil"),
    ],
    keywords=["stdlib", "io", "math", "process", "option", "result", "println",
              "string methods", "library", "mem", "memory", "allocator",
              "size_of", "align_of", "handle", "smart pointer", "vector",
              "collections", "structures", "sigil", "intrinsic", "builtin",
              "file", "files", "io", "open", "read", "write", "buffer",
              "subscript", "bounds", "Never", "some Iterator", "env",
              "environment variable", "random", "shuffle", "hash", "sha256",
              "crc32", "fnv", "json", "cli", "command line", "arguments",
              "date", "calendar", "weekday", "utcNow"]))


# ===========================================================================
# Diagnostics
# ===========================================================================
SECTIONS.append(Sec(
    "testing", "tooling", "Testing",
    "A test is an ordinary program. `rune test` builds and runs every file "
    "under `tests/`, and the exit status is the verdict — which is what "
    "`std::testing` produces for you.",
    [
        P("There is no test runner to configure and no attribute to remember. "
          "A file under `tests/` has a `main` like any other program, makes "
          "whatever checks it wants, and hands the tally back:"),
        S("""import std::testing

fn twice(n: i64) -> i64 { n * 2 }

fn main() -> i64 {
    testing::equal("twice doubles", twice(21), 42)
    testing::isTrue("and is monotonic", twice(3) > twice(2))
    testing::equal("this one fails on purpose", twice(2), 5)
    testing::summary()
}""", mode="fails", title="A test file, start to finish"),
        P("Every check prints its own line as it runs, so a failure names "
          "itself and says what it expected. `summary()` prints the tally and "
          "returns zero when everything passed and one otherwise, which is "
          "exactly what `main` should hand back."),

        H("The checks"),
        T(["Function", "Passes when"],
          [["`equal(name, got, want)`", "the two render the same"],
           ["`notEqual(name, got, want)`", "they do not"],
           ["`isTrue(name, cond)`", "the condition holds"],
           ["`isFalse(name, cond)`", "it does not"],
           ["`isSome(name, opt)`", "the Option holds something"],
           ["`isNone(name, opt)`", "it holds nothing"],
           ["`unreachable(name)`", "never — for a branch that should not have "
            "run at all"]]),
        N("`equal` compares values **as they print**. That covers every "
          "builtin exactly, and it means a type only has to be bound to "
          "`io::Display` to be testable — there is no separate equality bound "
          "to satisfy, which matters because a builtin like `i64` has built-in "
          "`==` rather than a binding to `operator::eq`.",
          label="Why equality is by rendering"),
        S("""import std::testing
import std::mem

fn main() -> i64 {
    let b = mem::Buffer<i64>(4)
    testing::isTrue("a fresh buffer holds its size", b.holds(3))
    testing::isFalse("and not past it", b.holds(9))
    testing::equal("its slots start zeroed", b[0], 0)

    // An Option, with the payload type given: it cannot be inferred from
    // nothing.
    testing::isNone::<String>("a counted slot is empty until written",
                              mem::Buffer<String>(2).at(0))

    let (passed, failed) = testing::counts()
    testing::equal("four checks ran", passed + failed, 4)
    testing::summary()
}""", mode="run", title="Checking a container"),

        H("Running them"),
        T(["Command", "Does"],
          [["`rune test`", "builds and runs every file under `tests/`"],
           ["`rune test --release`", "the same, optimised"],
           ["`rune test -v`", "shows each compiler command"]]),
        P("Each file is compiled as its own program and linked against the "
          "package, so a test can import the library it is testing by name. "
          "The summary line counts *files*; the per-check tally is printed by "
          "each file as it runs."),
        N("A test that aborts — a failed bounds check, an explicit "
          "`process::panic` — is a failure like any other, because the exit "
          "status is non-zero. You do not have to catch anything.",
          label="Aborts count as failures"),
        N("`rune new` scaffolds `tests/basics.rune` already written this way, "
          "so a fresh package has a passing test before you have written any "
          "code.", label="It is there from the start"),
    ],
    keywords=["test", "testing", "unit test", "assert", "equal", "isTrue",
              "isSome", "summary", "rune test", "tests/"]))

SECTIONS.append(Sec(
    "documentation", "tooling", "Documentation",
    "`rune doc` writes one page out of two halves that do not know about each "
    "other: what the compiler saw, and what you wrote under `docs/`.",
    [
        P("Prose attaches to a declaration in one of two ways. `@Doc(\"...\")` "
          "is the explicit form and takes a triple-quoted block for anything "
          "longer than a line; a `///` comment above the declaration does the "
          "same with less ceremony. Both are read at compile time and neither "
          "runs, so a library that is only ever linked against can still "
          "describe itself."),
        S('import std::io\n\n/// Greets a person in whichever language you ask for.\npub class hello {\n    pub name: String\n    fn init(self, name: String) { self.name = name }\n\n    @Doc("""\n    Greets in Spanish.\n\n    `Hola` is the everyday greeting, used at any hour and with anyone.\n    """)\n    pub fn spanish(&self) -> String { "Hola, " + self.name }\n\n    /// Greets in French, described by a comment rather than a decorator.\n    pub fn french(&self) -> String { "Bonjour, " + self.name }\n\n    pub fn korean(&self) -> String { "Annyeong, " + self.name }\n}\n\nfn main() -> i64 {\n    io::println(hello("Ada").spanish())\n    0\n}', mode="run", title="Two ways to say the same thing"),
        N("When a declaration has both, `@Doc` wins: the decorator was written "
          "for the reader, and the comment may only have been written for "
          "whoever is editing the code.", label="Which one is used"),

        H("The two halves"),
        P("`rune doc` builds one file, `docs/target/index.html`, and writes "
          "nothing else. What goes into it comes from two places that are "
          "independent of each other \u2014 either may be missing, and what is "
          "there is still built."),
        T(["Half", "Where it comes from"],
          [["**the reference**", "every declaration marked `pub`, with the "
            "comment that was on it, read out of the compiler"],
           ["**the guide**", "every `.md` file under `docs/`, read exactly as "
            "it is written"]]),
        P("`docs/` is yours. There is no naming convention to follow, no front "
          "matter to add and no marker to write around: a file is a page "
          "because it is there. The generator only ever reads it."),
        SH("""docs/
  index.md                  where the reader starts
  guide/
    index.md                names the group its siblings sit under
    usage.md
    examples.md
  target/index.html         the built page \u2014 the only thing generated"""),
        N("A package with no `src/` still builds its guide, and a package with "
          "no guide still builds its reference. Documentation for something "
          "that is only prose is the same command.",
          label="Either half on its own"),

        H("What the reference reports"),
        P("Everything public, and everything about one thing in one place. A "
          "type\u2019s fields, variants, associated types, methods and bindings "
          "are rendered beneath it rather than given pages of their own, "
          "because that is where a reader looks for them."),
        T(["Reported", "Sits under"],
          [["modules", "the top of the page"],
           ["`class`, `struct`, `enum`, `mark`", "their module"],
           ["fields, variants, associated types, methods",
            "the type they belong to"],
           ["`bind Mark to Type`", "the type, when it is one this package "
            "documents; the module otherwise \u2014 `bind i64 into Value` "
            "(the same as `bind As<Value> to i64`) is still public API"],
           ["`fn`, `macro`, `global`, `type` aliases", "their module"]]),
        P("A declaration with nothing written about it is one line: its "
          "signature already names it, and a heading above that would say the "
          "same word twice. One with prose gets a heading, because that is "
          "what a reader scans for. Non-public declarations get nothing "
          "\u2014 `init` is not documentation."),

        H("Reading it"),
        P("One file, and a tree down the left of it. Every top-level entry "
          "\u2014 each written page, each folder of them, each module \u2014 is a "
          "page of its own, and one is on screen at a time, with a link to "
          "the next at the foot. There are no numbers: clicking an entry "
          "opens what is under it and goes there. A page of prose expands to "
          "its `##`, `###` and `####` headings, nested as they nest in the "
          "page. Backticks in a heading become code in the tree and in the "
          "title, rather than showing the marks."),
        P("The tree follows the reader. Branches open as what they name comes "
          "into view and fold up again once it has passed \u2014 except the ones "
          "opened deliberately, which stay exactly as they were left."),
        T(["Control", "Does"],
          [["the menu icon", "folds the whole tree away, and brings it back"],
           ["the fold icon", "opens every branch, or closes every branch \u2014 "
            "and holds them there while you scroll"],
           ["the search icon, `/`, or `Ctrl+K`",
            "reveals the filter; matching branches open as you type"],
           ["`Enter` / `\u2193` / `\u2191`",
            "walks the matches, opening each as it goes"],
           ["`Esc`", "closes the filter and restores the tree"]]),
        P("The filter is words together, not a single string: `std io` finds "
          "`std::io`, and `?` finds the page that names it. Three letters or "
          "fewer have to be a whole word, so `for` does not light up `format`. "
          "Short tokens match a name, not the page body. Quotes hold a phrase "
          "together: `\"converts the error\"`. Matches are marked in the tree."),
        P("The arrow beside a name is what pins a branch open. Clicking the "
          "name itself goes there, and the tree folds back to the path of "
          "whatever is on screen \u2014 so scrolling through the document does "
          "not leave every visited folder standing."),
        P("Two entries may be called the same thing \u2014 a package\u2019s library "
          "and the program it builds are both `json` \u2014 so what tells them "
          "apart is a mark beside the name rather than a suffix bolted onto "
          "it. The same mark appears beside the heading and in the link at "
          "the foot of the page."),
        T(["Mark", "Means"],
          [["a page", "something written by hand, under `docs/`"],
           ["a folder", "a directory of written pages"],
           ["layers", "the library the package builds"],
           ["a prompt", "the program it builds"],
           ["brackets", "a module inside either"]]),
        P("A link between two written pages becomes a link within the "
          "document: `[usage](guide/usage.md)` reaches that page\u2019s section, "
          "because there is only ever one file."),

        H("Running it"),
        T(["Command", "Does"],
          [["`rune doc`", "builds, reads every output root, writes "
            "`docs/target/index.html`"],
           ["`rune doc --release`", "the same, from an optimised build"],
           ["`rune doc -v`", "shows each compiler invocation"]]),
        P("Each output root is read separately, with the package\u2019s "
          "components alongside it, exactly as the build compiles it \u2014 a "
          "binary that imports the package\u2019s own library is read the same "
          "way it is built. The records are then merged, so one page describes "
          "the whole package however many targets it produces."),
        N("The generator is `tools/rune-doc.rune` \u2014 a Rune program, using "
          "`std::io`\u2019s directory listing to find the guide. The toolchain "
          "documents itself with itself, and the sidecar it reads is one "
          "`key value` line per field, so a replacement generator needs a line "
          "loop and nothing else.", label="It is written in Rune"),
    ],
    keywords=["doc", "docs", "documentation", "@Doc", "///", "rune doc",
              "guide", "index.md", "generated", "sidebar", "search"]))

# ===========================================================================
# Builders
# ===========================================================================
SECTIONS.append(Sec(
    "builders", "abstraction", "Builders",
    "A tree of things is awkward to write as nested calls: the punctuation "
    "piles up at the end and the shape of the thing is lost in it. A builder "
    "block gives the shape back — and it is not special syntax for one "
    "type, but a rewrite anything can opt into.",
    [
        H("The block form"),
        P("`Name { ... }` whose contents are **values** rather than "
          "`field: value` pairs is a builder block. It stands for"),
        G("""{ var b = Name::empty()
  b.add(<first>)
  b.add(<second>)
  b }"""),
        P("so what a builder does is entirely up to the `add` it writes. "
          "Items are separated by a line break, a `;` or a `,`."),
        S("""import std::io
import std::builder
import std::collections::vector

struct Node { tag: String, text: String, style: String = "" }

extend Node {
    fn style(self, s: String) -> Node {
        Node { tag: self.tag, text: self.text, style: s }
    }
}

fn Text(t: String) -> Node { Node { tag: "text", text: t } }

struct Button { label: String = "ok" }

extend Button {
    fn style(self, s: String) -> Node {
        Node { tag: "button", text: self.label, style: s }
    }
}

struct Body { children: vector::Vector<Node> }

bind builder::Builder to Body {
    type Child = Node
    fn empty() -> Self { Body { children: vector::Vector<Node>() } }
    fn add(&var self, child: Node) { self.children.push(child) }
}

fn main() -> i64 {
    let page = Body {
        Text("Hello")
        Button {}.style("wide")
        Text("Bye").style("small")
    }
    var i = 0
    while i < page.children.length() {
        let c = page.children.at(i).unwrap()
        io::println(c.tag + " " + c.text + " [" + c.style + "]")
        i += 1
    }
    0
}""", mode="run", title="A page written as its own shape"),

        H("The mark"),
        P("`std::builder::Builder` is what a type binds to become one. Three "
          "things: what goes in, the empty one a block starts from, and how "
          "to take one more."),
        S("""pub mark Builder {
    /// What goes in.
    type Child

    /// The empty one, which a block starts from.
    fn empty() -> Self

    /// Takes one more. Called once per item, in the order they were written.
    fn add(&var self, child: Self::Child)
}""", mode="decls", title="std::builder"),
        N("`empty()` rather than a default value, because a builder may be a "
          "struct or a class and the two are built differently. One line says "
          "what an empty one is, and the block never has to know.",
          label="Why `empty`"),

        H("Telling it from a struct literal"),
        P("The first item decides. A **field** is a name followed by `:`, `,` "
          "or the closing brace; anything else is a **value**. So a single "
          "bare name is read as a field shorthand, and a trailing `;` is how "
          "a block holding one variable is written."),
        T(["Written", "Read as"],
          [["`Body { children: v }`", "a struct literal, one field"],
           ["`Body { child }`", "a struct literal, shorthand for "
            "`child: child`"],
           ["`Body { child; }`", "a builder block, one item"],
           ["`Body { Text(\"hi\") }`", "a builder block: `Text(\"hi\")` is "
            "not a field name"],
           ["`Body { }`", "a struct literal with no fields"],
           ["`Body { ..other }`", "a struct literal with a base"]]),
        S("""struct Point { x: i64, y: i64 }

fn main() -> i64 {
    let p = Point { 1; 2 }
    p.x
}""", mode="diag", title="A block of values on something that is not a builder"),

        H("Builders nest"),
        P("An item is an ordinary expression, so it may itself be a block. "
          "Nothing about the outer one has to know."),
        S("""import std::io
import std::builder
import std::collections::vector

struct Node { text: String }
fn Text(t: String) -> Node { Node { text: t } }

struct Body { children: vector::Vector<Node> }
bind builder::Builder to Body {
    type Child = Node
    fn empty() -> Self { Body { children: vector::Vector<Node>() } }
    fn add(&var self, child: Node) { self.children.push(child) }
}

struct Panel { parts: vector::Vector<Body> }
bind builder::Builder to Panel {
    type Child = Body
    fn empty() -> Self { Panel { parts: vector::Vector<Body>() } }
    fn add(&var self, child: Body) { self.parts.push(child) }
}

fn main() -> i64 {
    let panel = Panel {
        Body { Text("one") }
        Body { Text("two"); Text("three") }
    }
    io::println(panel.parts.length().$str())
    0
}""", mode="run", title="A builder whose children are built"),
        N("The rewrite happens in the parser, before anything is checked. "
          "Everything after that point — type checking, the borrow "
          "checker, code generation — sees a block, a local and a run of "
          "calls, which is why a builder behaves the same under either memory "
          "model and costs nothing a hand-written loop would not.",
          label="It is a rewrite, not a feature"),
    ],
    keywords=["builder", "Builder", "block", "add", "empty", "Child",
              "declarative", "tree", "DSL", "E0367"]))


SECTIONS.append(Sec(
    "macros", "abstraction", "Macros",
    "A macro is a rewrite from one run of tokens to another, chosen by "
    "pattern. Expansion happens before anything is parsed, so a macro can "
    "stand for whatever the grammar accepts — an expression, a run of "
    "statements, a declaration.",
    [
        S('import std::io\nimport std::collections::vector\n\nmacro twice {\n    ($x: expr) => { ($x) + ($x) }\n}\n\nmacro greet {\n    ($name: expr) => { io::println("hello " + $name) }\n    ($name: expr, $times: expr) => {\n        var i = 0\n        while i < $times { io::println("hi " + $name); i += 1 }\n    }\n}\n\nmacro ints {\n    ($($item: expr),*) => {\n        {\n            let built = vector::Vector<i64>()\n            $( built.push($item); )*\n            built\n        }\n    }\n}\n\nfn main() -> i64 {\n    // A one-expression macro splices as a unit, so it composes.\n    io::println((twice!(3) + 1).$str())\n\n    // Rules are tried in order; the arguments pick one.\n    greet!("ada")\n    greet!("bob", 2)\n\n    // A repetition, and the compiler\'s own `stringify!`.\n    io::println(ints!(1, 2, 3).length().$str())\n    io::println(stringify!(x > 0 && x != 3))\n    0\n}', mode="run", title="Definitions, rules and repetition"),
        P("A definition is `macro name { (pattern) => { expansion } }`, with "
          "as many rules as you like. They are tried in order and the first "
          "whose pattern fits the arguments wins. An invocation carries a `!`, "
          "so a reader always knows expansion is happening."),
        N("Definitions are gathered from every file before any is parsed, so a "
          "macro may be used above where it is written and across module "
          "boundaries — `vec!` lives in `std::collections::vector` and works "
          "anywhere.", label="Where a macro is in scope"),

        H("Patterns"),
        P("`$name: kind` captures a piece of the invocation. The kind says how "
          "much to take:"),
        T(["Kind", "Matches"],
          [["`expr`", "a balanced run of tokens, stopping at a top-level `,` "
            "or `;` — neither can occur inside one expression"],
           ["`ty`", "the same, for a type"],
           ["`ident`", "exactly one name"],
           ["`literal`", "one literal"],
           ["`block`", "a `{ ... }` group"],
           ["`tt`", "one token tree: a single token, or a balanced group"]]),
        P("`$( ... )sep*` matches a repetition, with `sep` between rounds and "
          "`*` or `+` for none-or-more and one-or-more. The body repeats once "
          "per round, and the separator goes **inside** it — `$( push($x); )*` "
          "— because the expansion needs one between statements, not the "
          "pattern."),

        H("Folding a repetition"),
        P("A repetition that has to combine its rounds — a sum, a product, an "
          "`&&` across every argument — puts the operator **inside** `$( )`, "
          "starting from the identity. `$( )*` already means \"once per "
          "round\"; there is no separate slot for something between rounds, "
          "so the operator comes along with each round and the identity gives "
          "the first one a left-hand side."),
        S('''import std::io

macro sum     { ($($item: expr),*) => { 0 $(+ $item)* } }
macro product { ($($item: expr),*) => { 1 $(* $item)* } }

fn main() -> i64 {
    // sum!(1, 2, 3)  ->  0 + 1 + 2 + 3
    io::println(sum!(1, 2, 3).$str())
    io::println(product!(2, 3, 4).$str())

    // Nothing to fold is just the identity, which is why it is there.
    io::println(sum!().$str())

    // The literal takes its type from the context, so this sums floats.
    let total: f64 = sum!(2.0, 4.0, 4.5)
    io::println(total.$str())
    0
}''', mode="run", title="Folding with the operator inside the repetition"),
        N("Writing `$($item)*` with the separator in the *pattern* only is a "
          "common slip: the pattern\'s `,` says how to read the arguments, "
          "not what to put between them. Such a macro expands to `1 2 3`, "
          "which is three statements, not a sum.",
          label="The pattern\'s separator is not the body\'s", tone="warn"),

        H("How an expansion is spliced"),
        P("An expansion that is a single expression is wrapped in parentheses "
          "so it composes like the one value it is. Without that, `twice!(3) + "
          "1` would expand to `(3) + (3) + 1` and quietly mean something "
          "else. An expansion that is several statements is spliced as it "
          "stands, since parenthesising it would not parse."),
        N("Which of the two is decided on the *expanded* tokens, not the rule "
          "body: a repetition hides its separator inside `$( )`, and only "
          "expansion brings it out to where it counts.",
          label="Decided after expansion, not before"),

        H("The macros the compiler supplies"),
        P("Two macros cannot be written as rules, because both need to see "
          "something a macro body no longer can. They are built in for that "
          "reason and no other."),
        T(["Macro", "Needs to see"],
          [["`stringify!(...)`", "the argument tokens as they were spelled"],
           ["`format!(\"...\", ...)`", "inside the format string literal"]]),
        P("`format!` is covered in **Formatting**; `println!` and `print!` are "
          "ordinary `pub macro`s written in terms of it, which is the usual "
          "shape: one built-in doing the part rules cannot, and rules on top."),
        S("""pub macro println {
    () => { std::io::println("") }
    ($($arg: expr),+) => { std::io::println(format!($($arg),+)) }
}""", mode="frag", title="`println!`, as the standard library writes it"),

        H("stringify!"),
        P("The argument tokens as the text they were written as. Nothing a "
          "macro body could do would produce it, because the spelling is gone "
          "by the time a body could look. It is what lets an assertion name "
          "itself:"),
        SH("""assert!(buffer.holds(3))
  ✓ buffer.holds(3)"""),
        P("`std::testing` uses it for `assert!`, `assertEq!` and `assertNot!`, "
          "each of which also takes an explicit name when the expression is "
          "not the explanation."),

        H("Macros that declare"),
        P("A body is spliced where it is called, so it may be anything the "
          "grammar accepts at that point \u2014 including declarations. A "
          "macro can stand for a type and its methods:"),
        S('''import std::io

macro Container {
    ($name: ident, $res: ty) => {
        struct $name { value: $res }
        extend $name {
            fn new(val: $res) -> Self { Self { value: val } }
        }
    }
}

Container!(Integer, i32)
Container!(Str, String)

fn main() -> i64 {
    io::println(Integer::new(42).value.$str())
    io::println(Str::new("hello").value)
    0
}''', mode="run", title="A macro standing for a type"),
        N("Such a macro is called at file scope, not inside a body. Types are "
          "collected before any body is checked, so one declared in a body is "
          "found too late to register \u2014 which the compiler now says "
          "outright rather than failing at the first use.",
          label="Declare at file scope", tone="warn"),

        H("When an expansion is wrong"),
        P("The code a macro stands for is not in the file, so an error inside "
          "one would otherwise point at a call and complain about something "
          "the reader cannot see. Every expanded token carries the range of "
          "the invocation, and the diagnostic carries the expansion with it:"),
        SH("""6 \u2551     io::println(sum!(1, "two", 3).$str())
                    ^^^ ERROR: cannot apply '+' to 'i64' and 'String'
    \u2500  note: in the expansion of `sum!(1, "two", 3)`
    \u2500  note:   which stands for: 0 + 1 + "two" + 3"""),
        P("A macro whose body calls another gives a chain, outermost first, "
          "so the note follows the same path the compiler did. A long chain "
          "keeps its two ends \u2014 the call as written, and the expansion "
          "that actually failed \u2014 and elides the middle."),
        N("This is why a macro is worth keeping small. The expansion is shown "
          "in the diagnostic, so a body that is a page of tokens produces a "
          "note that is a page of tokens.",
          label="Small bodies read better when they break"),

        H("Visibility"),
        P("A macro is private to the file it is written in. `pub macro` puts "
          "it in scope everywhere — in every module of the package, and in "
          "every package that builds against it."),
        SH("""macro helper { ... }        // this file only
pub macro vec { ... }       // everywhere"""),
        P("Reaching another package works because a `.rul` carries its "
          "modules' source alongside their object code. That source is read "
          "back before anything is parsed, so the macros in it join the table "
          "like any other: a package ships a macro the way it ships a "
          "function."),
        P("The default matters more here than it does for a function. "
          "Expansion is one pass over the whole compilation, so without a "
          "default of private, every helper macro anyone wrote would be in "
          "scope in every file at once, and two packages could not both have "
          "a `debug!` without colliding."),
        T(["Written", "In scope"],
          [["`macro name { ... }`", "the file it is written in"],
           ["`pub macro name { ... }`", "everywhere in the compilation"]]),
        N("A macro is not reached through its module: `vec!` is written "
          "`vec!`, never `vector::vec!`. Expansion happens before imports "
          "mean anything, so a macro name is one flat namespace — which is "
          "exactly why the private default is worth keeping.",
          label="Macros are not namespaced", tone="warn"),
        P("Two names collide when both are `pub`, or when both are in one "
          "file. Two private macros of the same name in different files never "
          "meet, so they are not a clash."),
        S("""macro helper {
    ($x: expr) => { ($x) * 2 }
}

fn main() -> i64 {
    // A macro that is not in scope is reported as such, rather than the
    // grammar tripping over the `!` further along.
    missing!(1)
    0
}""", mode="diag", title="A macro that is not there"),

        H("The macros the standard library provides"),
        P("A macro is in scope by name alone, but its body is not: `vec!` "
          "needs `import std::collections::vector` because that is what its "
          "expansion mentions. The printing macros name `std::io` in full and "
          "so need nothing."),
        T(["Macro", "Module", "Does"],
          [["`println!(\"{}\", x)`", "`std::io`",
            "writes a formatted line; `println!()` writes a blank one"],
           ["`print!(\"{}\", x)`", "`std::io`", "the same, without the newline"],
           ["`format!(\"{}\", x)`", "the compiler",
            "the `String` those two print; see **Formatting**"],
           ["`vec!(a, b, c)`", "`std::collections::vector`",
            "a `Vector` of the values given, written out like an array "
            "literal; `vec!()` is an empty one"],
           ["`assert!(cond)`", "`std::testing`",
            "checks it, and names the check after the expression itself"],
           ["`assert!(cond, name)`", "`std::testing`",
            "the same, when the expression is not the explanation"],
           ["`assertEq!(got, want)`", "`std::testing`",
            "compares, naming the check `got == want`"],
           ["`assertEq!(got, want, name)`", "`std::testing`", "with a name of "
            "your own"],
           ["`assertNot!(cond)`", "`std::testing`", "the negative of "
            "`assert!`"],
           ["`stringify!(...)`", "the compiler",
            "the argument tokens as the text they were written as"]]),
        S("""import std::testing
import std::collections::vector

fn twice(n: i64) -> i64 { n * 2 }

fn main() -> i64 {
    let v = vec!(1, 2, 3)

    assertEq!(v.length(), 3)
    assert!(twice(21) == 42)
    assertNot!(v.isEmpty())
    assert!(v.at(0).hasValue(), "the first element is there")

    testing::summary()
}""", mode="run", title="Using them"),
        P("Each assertion takes its name from `stringify!`, so a failing line "
          "in a test says what was being checked without the check having been "
          "named twice. Where the expression is not the explanation, pass a "
          "name and it is used instead."),

        H("What is reported"),
        T(["Written", "Reported"],
          [["arguments no rule fits", "`no rule of macro 'x' matches these "
            "arguments`, with the definition attached"],
           ["a macro that is not `pub`, used elsewhere", "`macro 'x' is not "
            "visible here`, naming the module it is private to"],
           ["a name that is no macro at all", "`no macro named 'x'`, listing "
            "the ones that are in scope"],
           ["a macro that expands to itself", "`macro expansion did not settle "
            "after 128 rounds`"],
           ["two macros of one name, either `pub`", "`macro 'x' is declared "
            "more than once`"],
           ["a rule without `=>` or `{ }`", "the piece that is missing, at the "
            "rule"]]),
        N("Errors inside an expansion point at the invocation, which is the "
          "only place the reader wrote anything.", label="Where an error lands"),

        H("When a pattern is not enough"),
        P("A pattern matches a shape. It cannot *compute* one: it has no way "
          "to count what it was given, read a name and derive another from "
          "it, or build a table out of its own entries. A macro that has to "
          "do any of that is written as **code** — an ordinary Rune function "
          "marked `@macro`, in a file that says it is a macro package."),
        S("""@type(Macros)

import std::Macro
import std::collections::vector

@macro
pub fn twice(input: Macro::Tokens) -> Macro::Tokens {
    let it = input.text()
    Macro::parse("((" + it + ") + (" + it + "))")
}""", mode="frag", title="macros.rune"),
        S("""fn main() -> i64 { twice!(3) }        // 6""",
          mode="frag", title="anywhere in the program"),
        P("A `@type(Macros)` file is a package of its own. The compiler builds "
          "it **first** — for the machine doing the compiling, whatever the "
          "program is being built for — and then runs it to expand each "
          "invocation. Two things follow, and they are why it is done this "
          "way:"),
        T(["", "Because it is compiled"],
          [["Everything works", "a macro is ordinary Rune. Generics, marks, "
            "`std::collections`, files — whatever it needs. There is no "
            "subset of the language to learn"],
           ["Nothing leaks", "what the package imports and declares is its "
            "own business. The program sees only the tokens that come back"],
           ["A crash is contained", "the package is a separate program, so a "
            "macro that fails is a failed expansion rather than a failed "
            "compiler"],
           ["It can be run by hand", "set `RUNE_MACRO_REQUEST` and "
            "`RUNE_MACRO_ANSWER` and run the package, and you can see "
            "exactly what it produces"]]),
        N("A cross build is no different: the macro package is built for the "
          "host and run there, and the program is built for the target.",
          label="Cross builds"),

        H("What a macro is given"),
        P("Tokens, with their structure kept. A bracketed group is **one** "
          "token — `block` for `{ ... }`, `parens` for `( ... )`, "
          "`brackets` for `[ ... ]` — whose `text()` is the group exactly as "
          "written and whose `inner()` is what is inside it."),
        T(["On a `Tokens`", "Is"],
          [["`length()`, `isEmpty()`", "how many tokens, counting a group as "
            "one"],
           ["`at(i)`", "the token there, or an empty one"],
           ["`slice(start, stop)`", "part of the run"],
           ["`text()`", "the run as source — or, for a run of one token, "
            "that token: a string literal's contents, a name's spelling"],
           ["`kind()`, `isA(kind)`", "`name`, `number`, `string`, "
            "`character`, `punctuation`, `keyword`, `block`, `parens`, "
            "`brackets`, or `run` for several"],
           ["`split(sep)`", "the parts between a piece of punctuation. A "
            "group is one token, so a comma inside brackets does not split"],
           ["`add(more)`", "append"],
           ["`flat()`", "every token, groups opened out"]],
          caption="`Macro::parse`, `ident`, `number`, `string`, `punct` and "
                  "`tokens` build them; `Macro::error` refuses."),
        S("""@type(Macros)

import std::Macro
import std::collections::vector
import std::text

/// One accessor per name given. A pattern could not: it has no way to make
/// `getX` out of `x`.
@macro
pub fn getters(input: Macro::Tokens) -> Macro::Tokens {
    var lines = vector::Vector<String>()
    for field in input.split(",") {
        let name = field.text()
        let capital = text::upper(name.$substring(0, 1)) +
                      name.$substring(1, name.$length())
        lines.push("extend Point { fn get" + capital +
                   "(&self) -> i64 { self." + name + " } }")
    }
    Macro::parse(text::join(lines, "\\n"))
}

/// Refuses what it cannot use. The message is reported against the
/// invocation, with an arrow back at this line.
@macro
pub fn firstWord(input: Macro::Tokens) -> Macro::Tokens {
    if input.isEmpty() { Macro::error("firstWord! needs a word") }
    if !input.at(0).isA("name") {
        Macro::error("firstWord! wants a name, and this is a " +
                     input.at(0).kind())
    }
    Macro::string(input.at(0).text())
}

/// A block goes in as one token and comes back as written.
@macro
pub fn traced(input: Macro::Tokens) -> Macro::Tokens {
    let label = input.at(0).text()
    let body = input.at(input.length() - 1)
    if !body.isA("block") {
        Macro::error("traced! wants a block last, and got a " + body.kind())
    }
    Macro::parse("{ io::println(\\"enter " + label + "\\"); " +
                 body.inner().text() + " }")
}""", mode="frag", title="Three macros"),
        N("Because a block passes through untouched, whatever is inside it is "
          "compiled in the **program**. That is how a macro reaches "
          "`std::reflect`: `Macro::parse(\"reflect::typeName<\" + t + \">()\")` "
          "answers about the program's types, not the package's.",
          label="Reflection, through an expansion"),

        H("What is reported, for these"),
        T(["Written", "Reported"],
          [["`@macro` outside a macro package", "`a procedural macro belongs "
            "in a macro package`, naming `@type(Macros)`"],
           ["a `@macro fn` without `pub`", "a macro has to be `pub` — the "
            "dispatcher the compiler writes is another module"],
           ["`Macro::error(\"...\")`", "that message, against the "
            "invocation, with an arrow at the line that refused"],
           ["a macro that does not finish", "`macro 'x' did not finish`, with "
            "the status or signal and how to run it yourself"],
           ["an expansion that does not lex", "`macro 'x' produced text that "
            "is not valid Rune`"]]),
    ],
    keywords=["macro", "macro_rules", "expand", "stringify", "repetition",
              "pattern", "assert", "vec", "token", "procedural", "@macro",
              "@type(Macros)", "Macro::Tokens", "proc macro", "block"]))

SECTIONS.append(Sec(
    "diagnostics", "feedback", "Reading a diagnostic",
    "Every message has the same anatomy, so once you can read one you can read "
    "all of them. The output on this page is captured from the compiler, not "
    "transcribed.",
    [
        H("The parts"),
        T(["Part", "Looks like", "Means"],
          [["header", "`┌ ERROR[E0230] in file.rune`", "severity, code, file"],
           ["gutter", "`12 ║`", "the line number, then your source"],
           ["carets", "`^^^^`", "exactly the span at fault"],
           ["`note:`", "`● note: …`", "why the compiler thinks so"],
           ["`hint:`", "`○ hint: …`", "something you could try"],
           ["related", "`└▶ …`", "a second place that matters"]]),
        P("A type mismatch is the shape to learn first: what was wanted, what "
          "arrived, and where each came from."),
        S("""fn area(width: i64, height: i64) -> i64 {
    width * height
}

fn main() -> i64 {
    area(3, "four")
}""", mode="diag", title="A type mismatch"),
        P("When the mistake has an obvious repair, the hint says it outright."),
        S("""struct Point { x: i64, y: i64 }

fn main() -> i64 {
    let p = Point { x: 1, y: 2 }
    p.z
}""", mode="diag", title="A hint that names the fix"),
        P("A second location appears as a related note whenever the "
          "explanation is somewhere other than the error."),
        S("""fn main() -> i64 {
    let total = 10
    total = 11
    total
}""", mode="diag", title="Assigning to an immutable binding"),
        P("Inside a generic, the failure is in the template but the *cause* is "
          "the call. Both are shown."),
        S("""fn largest<T: Comparable>(a: T, b: T) -> T {
    if a > b { a } else { b }
}

struct Opaque { tag: i64 }

fn main() -> i64 {
    let winner = largest(Opaque { tag: 1 }, Opaque { tag: 2 })
    winner.tag
}""", mode="diag", title="A failure inside an instantiation"),
        P("Exhaustiveness is checked, and the message lists precisely what you "
          "left out."),
        S("""enum Signal { Red, Amber, Green }

fn main() -> i64 {
    let s = Signal::Amber
    match s {
        Signal::Red => 0
        Signal::Green => 1
    }
}""", mode="diag", title="A match with a gap"),

        H("Warnings"),
        P("Warnings follow the same shape in yellow. `-Werror` promotes them, "
          "`-w` silences them, and `--error-limit <n>` stops the compiler after "
          "*n* errors so a single mistake does not fill your terminal."),
        S("""import std::io

fn main() -> i64 {
    let unused = 99
    io::println("hello")
    0
}""", mode="run", title="A warning does not stop the build"),

        H("Error codes"),
        T(["Range", "Raised by"],
          [["E01xx", "lexing and parsing"],
           ["E02xx", "names, types and type checking"],
           ["E03xx", "mutability, ownership and borrows"],
           ["E04xx", "marks, conformance and generics"],
           ["E05xx", "layout and code generation"],
           ["E06xx", "modules, imports and linking"]],
          caption="The code is stable, so it is a reasonable thing to search "
                  "for or to match in a test."),
    ],
    keywords=["error", "warning", "diagnostic", "note", "hint", "code",
              "E0230", "message"]))


# ===========================================================================
# Toolchain
# ===========================================================================
SECTIONS.append(Sec(
    "toolchain", "tooling", "The toolchain",
    "`runec` compiles files. `rune` manages packages and calls `runec`. For "
    "anything bigger than one file, use `rune`.",
    [
        H("runec"),
        T(["Flag", "Does"],
          [["`-o <path>`", "output path"],
           ["`-c`", "emit an object file and stop"],
           ["`--emit-llvm`", "emit textual LLVM IR"],
           ["`--emit-asm`", "emit target assembly"],
           ["`--emit-lib`", "emit a `.rul` library"],
           ["`--shared`", "emit a native shared library "
            "(`.dylib` / `.so` / `.dll`)"],
           ["`--check`", "type-check only, produce nothing"],
           ["`-O0` … `-O3`", "optimisation level, default `-O0`"],
           ["`-g`", "emit debug information"],
           ["`--target <triple>`", "cross-compile; see **Cross compilation**"],
           ["`--cc <program>`", "the toolchain driver used to link"],
           ["`--sysroot <dir>`", "the target's headers and libraries"],
           ["`--runtime-dir <dir>`", "where `libruneruntime.a` is"],
           ["`--link-arg <arg>`", "appended to the link command verbatim"],
           ["`--link-cxx`", "link the C++ runtime (implied by `extern \"C++\"`)"],
           ["`--safety <level>`", "`none`, `minimal` or `full` (default)"],
           ["`--memory <mode>`", "`zombie` (default) or `arc`; see **Single "
            "ownership without a count**"],
           ["`--no-zombie-stdlib`", "silence Zombie findings inside the standard "
            "library (reported by default)"],
           ["`-I <dir>`", "add a module search path"],
           ["`-L <dir>` / `-l <name>`", "native library path / library"],
           ["`--module <name>`", "set the module name"],
           ["`--cfg <name>`", "set *name* for `@Config(...)`"],
           ["`--stdlib <dir>`", "where the standard library lives"],
           ["`--no-stdlib`", "do not import it implicitly"],
           ["`-Werror` / `-w`", "warnings as errors / silence warnings"],
           ["`--error-limit <n>`", "stop after *n* errors, `0` for unlimited"],
           ["`--color` / `--no-color`", "force colour on or off"],
           ["`--dump-tokens`", "print the token stream"],
           ["`--dump-ast`", "print the parse tree"],
           ["`--dump-symbols`", "print the symbol table"],
           ["`--dump-types`", "print the type of every expression"],
           ["`-v`", "report each pipeline stage"],
           ["`--time`", "report how long each stage took"]]),
        T(["Environment", "Does"],
          [["`RUNE_JOBS`", "threads to lex, parse and check with; "
            "default one per core"],
           ["`RUNE_CC`", "the link driver, when `--cc` does not name one"]]),
        SH("""$ runec -o hello hello.rune                     # compile and link
$ runec --check src/*.rune                       # just type-check
$ runec --emit-llvm -O2 -o hot.ll hot.rune       # look at the IR
$ runec --safety none -O3 -o fast bench.rune     # no checks at all
$ runec --dump-ast small.rune | head -40         # see the parse tree
$ runec --time -o hello hello.rune               # where the time went"""),

        H("Where the time goes"),
        P("`--time` reports each stage of a compile. The numbers are wall "
          "clock rather than CPU time — lexing, parsing and the ownership "
          "pass spread themselves over the cores they can see, and should "
          "read as the time they took rather than as the sum of what every "
          "thread spent. The header says how many threads were available so "
          "the two are not confused."),
        SH("""$ runec --time --check src/main.rune
  time report (8 threads available)
    read          0.0 ms    0.0 %
    lex           9.2 ms   15.3 %
    macros        0.8 ms    1.4 %
    parse         3.8 ms    6.3 %
    check        42.0 ms   77.0 %
    ----------------------------
    total        60.2 ms  100.0 %"""),
        P("`check` is usually the largest, because a compilation reads the "
          "whole standard library in order to understand its own code. It is "
          "read, not emitted: what ends up in the artefact is only what the "
          "artefact reaches."),

        H("What ends up in the artefact"),
        P("Code the artefact owns — everything in the files named on the "
          "command line — is always emitted, whether or not something in it "
          "is called, because a `.rul` has to carry every public thing it "
          "declares. Code it merely carries a copy of — the standard library, "
          "an imported library's generics — is emitted only where this "
          "artefact reaches it, and what is left is dropped before the back "
          "end sees it."),
        P("Nothing is lost by that. A dropped definition is one no call, no "
          "vtable slot, no initialiser and no metadata in the module "
          "mentions, and another artefact that wants it carries its own copy. "
          "A hello-world object holds seventeen functions rather than nine "
          "hundred."),

        H("Debug builds"),
        P("`-g` emits DWARF — a compile unit, a subprogram per function with "
          "its source file and line, a type for every parameter and local, and "
          "a location on every instruction — so a debugger can break, step, "
          "and print variables by name."),
        SH("""$ runec -g -o list list.rune
$ lldb ./list
(lldb) b list.rune:24
(lldb) run
(lldb) frame variable"""),
        P("A `-g` build also keeps a frame pointer in every function and "
          "exports its symbols, which is what lets the runtime print a "
          "traceback when a program aborts. The frames are demangled back to "
          "the names you wrote."),
        S("""import std::io

struct Grid { cells: [4:i64] }

extend Grid {
    pub fn at(&self, i: i64) -> i64 { self.cells[i] }
}

fn reach(g: Grid, depth: i64) -> i64 {
    if depth == 0 { return g.at(9) }
    reach(g, depth - 1)
}

fn main() -> i64 {
    let g = Grid { cells: [1, 2, 3, 4] }
    io::println("about to go out of range")
    reach(g, 2)
}""", mode="panic", title="A traceback (this page is built with `-g`)"),
        N("Without `-g` the panic still names the file and line, but no "
          "traceback is printed: the frame pointer and the symbol names are "
          "not there to walk.", label="Release builds stay quiet"),

        H("rune"),
        T(["Command", "Does"],
          [["`rune new <name>`", "a package in a new directory"],
           ["`rune new <name> --lib`", "the same, as a library"],
           ["`rune init`", "a package here"],
           ["`rune build`", "build this package and its dependencies"],
           ["`rune run [-- args]`", "build, then run, forwarding arguments"],
           ["`rune test`", "build and run every program under `tests/`"],
           ["`rune check`", "type-check without producing output"],
           ["`rune doc [--open]`", "read `docs/` and the source; write `target/<profile>/docs`, and show it"],
           ["`rune doc std::io`", "the standard library's reference, from the cache, at that module"],
           ["`rune targets`", "the cross targets this package configures"],
           ["`rune clean`", "delete `target/`"]]),
        T(["Option", "Does"],
          [["`--release`", "`-O2`, no debug information"],
           ["`--target <name>`", "a `[target.<name>]` toolchain, or a triple"],
           ["`--emit <kind>`",
            "`llvm-ir`, `asm`, `obj`, `lib` or `exe` — what to produce "
            "instead of linking"],
           ["`-C, --directory <d>`", "operate on the package in *d*"],
           ["`-j, --jobs <n>`",
            "compile at most *n* things at once; default one per core"],
           ["`--cfg <name>`",
            "set *name* for `@Config(...)`, on top of `[build] cfg`"],
           ["`-v, --verbose`", "print each command as it runs"],
           ["`--no-color`", "plain output"]]),
        SH("""$ rune new report && cd report
$ rune run
$ rune run -- 1 2 3 4 5
$ rune test
$ rune build --release
$ rune build -j 4"""),

        H("How a build decides what to do"),
        P("`rune` resolves every package reachable from the root before it "
          "compiles any of them, and turns the result into steps ordered only "
          "by what genuinely needs what. A package's library is one step; each "
          "binary it produces is another, waiting only on that library; a "
          "dependent's library waits on the library it depends on and on "
          "nothing else."),
        P("Steps with no edge between them run at the same time. Two sibling "
          "dependencies are compiled together, six binaries of one package are "
          "compiled together once its `.rul` exists, and so is every program "
          "under `tests/`. A step's output is collected and printed in one "
          "piece when it finishes, so two compiles failing at once produce two "
          "diagnostics rather than one interleaved mess."),
        P("A step is skipped when a digest of everything it reads matches the "
          "one recorded beside its output: the command line, the contents of "
          "every source file, the contents of every `.rul` it imports, the "
          "manifest, the compiler binary, and every source file of the "
          "standard library."),
        N("Contents, not timestamps. Touching a file changes nothing, and "
          "neither does checking it out again, copying the tree or restoring "
          "it from a cache — while a change that arrives with an older "
          "timestamp than the output is still noticed. The standard library "
          "counts as an input because it is compiled from source into "
          "everything, so editing it rebuilds what used it.",
          label="Why the digest, rather than mtimes"),
        SH("""$ rune build
○ Compiling geometry v0.1.0 (library)
○ Compiling app v0.1.0
● Finished debug profile

$ rune build              # nothing has changed
● Finished debug profile

$ touch src/main.rune
$ rune build              # still nothing: the bytes are the same
● Finished debug profile"""),
        P("Flags that change what is produced are part of the digest, so "
          "`--release`, `--target` and the `[build]` settings each get their "
          "own answer. Flags that only change how the build narrates itself "
          "are not, so `rune build -v` after `rune build` recompiles nothing. "
          "`rune clean` deletes `target/`, digests included."),

        H("Producing something other than a program"),
        P("`--emit` builds every root the package owns into the asked-for "
          "form instead of linking it: one file per root under "
          "`target/<profile>/`, named after the root. It is what to reach for "
          "when the question is *what did the compiler make of this* rather "
          "than *does it run*."),
        SH("""$ rune build --emit llvm-ir     # target/debug/<name>.ll, per root
$ rune build --emit asm         # .s
$ rune build --emit obj         # .o
$ rune build --emit lib         # .rul, even for a package with a main"""),
        P("Dependencies still build as libraries whatever `--emit` says, "
          "because a `.rul` is what this package needs from them in order to "
          "compile at all — an `.ll` in its place would leave nothing to "
          "import. A package that also produces a library keeps producing it: "
          "the emitted file is the same code in another form, not a "
          "replacement for the artefact a dependent links against."),
        N("`[build] emit` in the manifest says the same thing when the "
          "command line does not, and `@type(...)` on a single file says it "
          "for that file alone. The command line wins over both.",
          label="Three ways to ask"),

        H("Package layout"),
        S("""report/
├── Rune.toml           # the manifest
├── src/
│   ├── main.rune       # binary root  -> target/<profile>/report
│   └── lib.rune        # library root -> target/<profile>/report.rul
├── tests/
│   └── basics.rune     # one test program per file
└── target/
    ├── debug/
    └── release/""", mode="frag", title="Conventional structure"),
        N("A package may have both roots. `src/main.rune` becomes the "
          "executable, `src/lib.rune` the library other packages import.",
          label="Both at once"),

        H("Rune.toml"),
        S("""[package]
name = "report"                 # required
version = "0.1.0"               # required
edition = "2025"
description = "Command line front end for the statistics library"
authors = ["you <you@example.com>"]
license = "MIT"

[build]
safety = "full"                 # none | minimal | full
memory = "zombie"               # zombie | arc (reference counting)
emit = "exe"                    # exe | lib | obj | asm | llvm-ir
optimize = 0                    # 0..3, or use --release
debug = true
warnings-as-errors = false
no-stdlib = false
link = ["m"]                    # -l, inherited by dependents
link-paths = ["/usr/local/lib"] # -L
c-sources = ["c/shim.c"]        # compiled with the build's own toolchain
c-flags = ["-Wall"]
cxx-sources = ["cxx/shim.cpp"]  # a C++ half, built by the matching driver
cxx-flags = ["-Wall"]
cxx-standard = "c++17"
link-args = ["-Wl,-z,now"]      # passed to the linker verbatim
target = "mingw"                # build for this target unless told otherwise

[target.mingw]                  # `rune build --target mingw`
triple = "x86_64-w64-mingw32"
cc = "x86_64-w64-mingw32-gcc"
cxx = "x86_64-w64-mingw32-g++"  # derived from `cc` when not given
runner = "wine"                 # how to run one of its binaries here

[dependencies]
statistics = { path = "../statistics" }

[lib]
src = "src/lib.rune"            # override the default root

[bin]
src = "src/main.rune"

[tests]
src = "tests"                   # where `rune test` looks""",
          mode="frag", title="Every key the manifest understands"),
        T(["Table", "Key", "Default"],
          [["`[package]`", "`name`", "*required*"],
           ["", "`version`", "*required*"],
           ["", "`edition`", "`\"2025\"`"],
           ["", "`description`, `authors`, `license`", "empty"],
           ["`[build]`", "`safety`", "`\"full\"`"],
           ["", "`emit`", "empty, meaning an executable"],
           ["", "`optimize`", "`0`, or `2` with `--release`"],
           ["", "`debug`", "`true`, `false` with `--release`"],
           ["", "`warnings-as-errors`", "`false`"],
           ["", "`no-stdlib`", "`false`"],
           ["", "`link`, `link-paths`", "empty; inherited by dependents"],
           ["", "`c-sources`, `c-flags`", "empty"],
           ["", "`cxx-sources`, `cxx-flags`", "empty; a C++ half"],
           ["", "`cxx-standard`", "`\"c++17\"`"],
           ["", "`link-args`", "empty; passed to the linker verbatim"],
           ["", "`target`", "empty, meaning the host"],
           ["`[target.<name>]`", "`triple`", "*required*"],
           ["", "`cc`, `cxx`, `ar`", "the host's, which usually cannot cross"],
           ["", "`sysroot`, `runtime-dir`", "empty"],
           ["", "`runner`", "empty: its binaries cannot be run here"],
           ["", "`link`, `link-paths`, `link-args`", "empty"],
           ["`[dependencies]`", "`name = { path = \"...\" }`", "—"],
           ["`[lib]`", "`src`", "`src/lib.rune`"],
           ["`[bin]`", "`src`", "`src/main.rune`"],
           ["`[tests]`", "`src`", "`tests`"]]),

        H("Dependencies"),
        P("A dependency is a path, or a version from a registry. `rune build` "
          "builds each one first, emits its `.rul`, and passes both the module "
          "search path and every native library it asked for down to whatever "
          "depends on it — so a package that needs `-l m` says so once, in its "
          "own manifest."),
        SH("""[dependencies]
geometry = { path = "../geometry" }    # a package on this disk
shapes = "1.0"                         # from a registry: ^1.0, the newest 1.x
report = { version = "=0.3.2" }        # exactly that version"""),
        T(["Requirement", "Accepts"],
          [["`\"1.2.3\"`, `\"^1.2.3\"`", ">=1.2.3 and <2.0.0 — the same leading non-zero part"],
           ["`\"0.2\"`", ">=0.2.0 and <0.3.0"],
           ["`\"~1.2\"`", ">=1.2.0 and <1.3.0"],
           ["`\"=1.2.3\"`", "exactly that"],
           ["`\">=1.2, <2.0\"`", "every comparison listed"],
           ["`\"*\"`", "anything"]],
          caption="Cargo's spellings, since they are the ones people know."),
        SH("""$ cd examples/project/report
$ rune run
$ rune test"""),

        H("Packages from a registry"),
        P("The commands, in brief; [Packages and registries](#packages) walks "
          "through making a package, using one, and running a registry."),
        P("A registry is a directory of static files: an `index.toml` that "
          "lists every release — name, version, description, checksum, "
          "dependencies — and a `packages/` tree of plain `tar` archives. "
          "Serving one is serving files; mirroring one is copying a "
          "directory; a registry on a shared drive needs no server at all. "
          "The index is fetched once and cached, so searching costs nothing "
          "after the first look, and dependency resolution needs nothing but "
          "the index — every archive a build needs is known before the first "
          "byte of one is downloaded, which is what lets them all download at "
          "once."),
        T(["Command", "Does"],
          [["`rune pkg init [dir] [--name N]`", "make a registry here, or in *dir*, named after it or *N*"],
           ["`rune registry --addPackage <project> [--dir D]`",
            "pack a project and add it to the registry's index"],
           ["`rune registry --serve [--port N] [--dir D]`",
            "serve the registry over HTTP (default port 7878)"],
           ["`rune registry add <url> [--name N]`",
            "use a registry from this machine — `http://`, `file://`, or a path — under its own name, or the alias *N*"],
           ["`rune registry list`", "the registries this machine uses"],
           ["`rune registry remove <name>`", "stop using one; what came from it stays installed"],
           ["`rune search <regex>`", "packages whose name or description match"],
           ["`rune desc <name>`", "versions, authors, dependencies, where it is from, whether it is installed"],
           ["`rune add <name>[@req]`", "depend on it: install, write `Rune.toml`, pin in `Rune.lock`"],
           ["`rune remove <name>`", "drop the dependency; with no name, uninstall what nothing uses"],
           ["`rune update [<name>]`", "move to the newest versions the requirements allow"],
           ["`rune deps`", "the dependency tree, each package tagged with its registry"],
           ["`rune installed`", "every installed version, how many projects use it, and where it came from"],
           ["`rune doc <name>`", "a package's documentation: the copy this project uses, or the newest, fetched if need be"]],
          caption="A package is `<name>`, or `<registry>::<name>` to take it from one "
                  "registry; `search`, `desc`, `add`, `update`, `deps` and `installed` "
                  "take `--registry <name>` for the same."),
        P("Installed packages live under `~/.rune/pkg/<name>/<version>/`, "
          "once each however many projects use them. Each keeps the list of "
          "projects that reference it, so `rune installed` can say which "
          "versions nothing uses any more and `rune remove` with no arguments "
          "can uninstall them. A project pins what it resolved in `Rune.lock` "
          "— commit it, and a build on another machine fetches exactly the "
          "same versions, from the cache when it has them."),
        SH("""$ rune pkg init registry --name work
$ rune registry --addPackage ../geometry --dir registry
$ rune registry --serve --dir registry &
$ rune registry add http://localhost:7878
● Added registry 'work' at http://localhost:7878 (1 release)
$ rune search geo
geometry  v0.2.0     Points and distances
$ rune add work::geometry
○ Fetching geometry v0.2.0
● Installed geometry v0.2.0
● Added geometry "0.2.0" from work to Rune.toml (v0.2.0 installed)
$ rune deps
app v0.1.0
└─ geometry v0.2.0 (0.2.0) [work]
$ rune doc geometry"""),
        N("A registry added with `rune registry add` is not monitored: "
          "nothing reviews what it serves. The archive's checksum is checked "
          "against the index on every install, which catches a corrupted or "
          "tampered file, not a malicious package. Read what you depend on.",
          label="Trust", tone="warn"),
    ],
    keywords=["runec", "rune", "cli", "flags", "toml", "manifest", "build",
              "test", "package", "toolchain", "incremental", "parallel",
              "jobs", "cache", "rebuild", "time", "fingerprint", "registry",
              "search", "add", "remove", "update", "deps", "installed",
              "Rune.lock", "version", "registry", "registry name",
              "--registry", "rune doc"]))


# ===========================================================================
# Packages and registries
# ===========================================================================
SECTIONS.append(Sec(
    "packages", "tooling", "Packages and registries",
    "How a package is made, how one is used, and how a registry is run. A "
    "package is a directory with a `Rune.toml`; a registry is a directory of "
    "static files that any `rune` can install from. `examples/package/` is "
    "all of this worked through: seven packages, two programs and the "
    "registry that serves them.",
    [
        H("Making a package"),
        P("`rune new <name> --lib` lays one out. What it produces is the "
          "whole convention: a manifest, a `src/` whose `lib.rune` is the "
          "package's module, and room for tests and documentation."),
        SH("""$ rune new geometry --lib
$ tree geometry
geometry
├── Rune.toml
├── src
│   └── lib.rune        the package module: `import geometry` reaches this
├── tests               one program per file; `rune test` runs them
└── docs                prose; `rune doc` renders it beside the API"""),
        P("The manifest says what the package is. The first three fields are "
          "what a registry shows and what `rune search` matches against; "
          "`version` is what everything else keys on."),
        SH("""[package]
name = "geometry"
version = "0.3.0"
edition = "2025"
description = "Points and vectors on the plane: distances, dot products, and the arithmetic between them"
authors = ["The Rune examples"]
license = "MIT"

[build]
safety = "full"

[dependencies]"""),
        P("What a package offers is what it marks `pub`. A `///` comment above "
          "a public declaration is its documentation, and `docs/index.md` is "
          "the page a reader starts at — both end up in `rune doc`'s output "
          "and travel with the package into a registry."),
        S("""// src/lib.rune — geometry, the plane and what can be done on it.

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
}""", mode="decls", title="A library module"),
        P("A test is an ordinary program under `tests/`. It imports the "
          "package by name, exactly as a user would, and reports through "
          "`std::testing`; `rune test` builds and runs every file there and "
          "reads the exit status."),
        SH("""// tests/basics.rune
import std::testing
import geometry

fn main() -> i64 {
    let a = geometry::point(0.0, 0.0)
    let b = geometry::point(3.0, 4.0)
    testing::equal("distance", geometry::distance(a, b), 5.0)
    testing::summary()
}"""),
        SH("""$ rune test
○ Compiling geometry v0.3.0 (library)
○ Compiling test basics
  ✓ distance
  1 passed
● Test result: 1 file(s) passed, 0 failed
$ rune doc --open"""),

        H("What a version promises"),
        P("Versions are `major.minor.patch`, and a requirement written without "
          "an operator — `\"0.3\"`, `\"1.2.3\"` — accepts anything with the "
          "same leading non-zero part: `\"1.2\"` takes 1.9.0 but not 2.0.0, "
          "and `\"0.3\"` takes 0.3.7 but not 0.4.0. That is the promise a "
          "version makes: a change to the leading part may break a caller, "
          "and anything smaller must not. The rule of thumb, then:"),
        T(["Bump", "When"],
          [["patch", "a fix; nothing public changed"],
           ["minor", "something public was added; nothing was taken away or changed"],
           ["major (or the minor, below 1.0)", "something public changed or went away"]],
          caption="`stats` 2.0.0 → 2.1.0 added `percentile`; `units` 1.0.0 → 1.1.0 added `Temperature`."),
        P("What a dependency may ask for, then, in the manifest or after `@` "
          "on the command line (`rune add stats@~2.0`):"),
        T(["Requirement", "Accepts"],
          [["`\"2.1.0\"`, `\"^2.1.0\"`", ">=2.1.0 and <3.0.0 — the same leading non-zero part"],
           ["`\"2.1\"`, `\"2\"`", "the same, with the missing parts read as 0: >=2.1.0 <3.0.0; >=2.0.0 <3.0.0"],
           ["`\"0.3\"`", ">=0.3.0 and <0.4.0 — below 1.0, the minor is the leading part"],
           ["`\"~2.1\"`", ">=2.1.0 and <2.2.0"],
           ["`\"=2.1.0\"`", "exactly that"],
           ["`\">=2.0, <2.2\"`", "every comparison listed — `>=`, `>`, `<=`, `<` and `=`, separated by commas"],
           ["`\"*\"`", "anything"]],
          caption="Cargo's spellings, since they are the ones people know. The "
                  "same table is under *The toolchain → Dependencies*."),
        P("A requirement is checked against what exists whenever it is "
          "acted on: `rune add stats@=2.2.0` refuses when no such version "
          "is in any registry, and a manifest edited by hand to say so is "
          "caught by the next `rune build` — the lock's pin is not trusted "
          "past what the manifest now asks for, so the build resolves "
          "again and fails with the versions there are. `rune deps` marks "
          "such a pin *stale* until then."),

        H("Using a package"),
        P("A dependency is a path, for a package on this disk, or a version, "
          "for one from a registry. Either way `rune build` builds it first "
          "and passes its `.rul` — and every native library it asked for — "
          "down to whatever depends on it."),
        SH("""[dependencies]
geometry = { path = "../geometry" }              # while both are being written
shapes = "1.0"                                   # from a registry: ^1.0, the newest 1.x
logger = { version = "1.0", registry = "lab" }   # from that registry alone"""),
        P("`rune add` writes the line, installs the package and everything it "
          "needs, and pins the result in `Rune.lock`. The other commands work "
          "on the same three things — manifest, lock, install store."),
        T(["Command", "Manifest", "Lock", "Store"],
          [["`rune add shapes[@1.0]`", "adds `shapes = \"…\"`", "pins shapes and what it needs", "installs what is missing"],
           ["`rune build` / `run` / `test`", "—", "written if a dependency is not yet pinned", "installs what is missing"],
           ["`rune update [shapes]`", "—", "moves to the newest allowed", "installs what is missing"],
           ["`rune remove shapes`", "drops the line", "unpins what nothing needs", "drops this project's reference"],
           ["`rune remove`", "—", "—", "uninstalls every version no project uses"],
           ["`rune deps`", "reads", "reads", "reads"],
           ["`rune installed`", "—", "—", "lists every version and who uses it"],
           ["`rune doc shapes`", "—", "reads, to pick the version", "reads; installs the newest if none is there"]]),
        SH("""$ rune add report
○ Fetching report v1.2.0
○ Fetching plot v0.5.0
○ Fetching stats v2.1.0
○ Fetching units v1.1.0
○ Fetching geometry v0.3.0
● Installed geometry v0.3.0
● Installed units v1.1.0
● Installed stats v2.1.0
● Installed plot v0.5.0
● Installed report v1.2.0
● Added report "1.2.0" to Rune.toml (v1.2.0 installed)
$ rune deps
dashboard v0.1.0
└─ report v1.2.0 (1.2.0)
   ├─ plot v0.5.0 (0.5)
   │  ├─ geometry v0.3.0 (0.3)
   │  └─ stats v2.1.0 (2.0)
   ├─ stats v2.1.0 (2.1)
   └─ units v1.1.0 (1.1)"""),
        P("One version of a package serves everyone in a build: `plot` asks "
          "for `stats 2.0` and `report` for `stats 2.1`, and 2.1.0 satisfies "
          "both. Two requirements no single version satisfies are an error "
          "that names them. Two *projects* may want different versions — "
          "`examples/package/apps/legacy` pins `units = \"=1.0.0\"` while "
          "`dashboard` uses 1.1.0 — and both are installed, each referenced "
          "by the project that uses it."),
        N("Commit `Rune.lock`. A build on another machine then fetches "
          "exactly the versions this one resolved, and `rune update` is the "
          "only thing that moves them.", label="The lock"),
        P("A package's documentation is a command away, whether or not the "
          "project here uses it. `rune doc shapes` builds and opens the "
          "documentation of the copy this project's lock pins — or, outside "
          "a project, the newest version installed — and when nothing is "
          "installed it fetches the newest release a registry has, which "
          "then sits in the store unreferenced until `rune remove` reclaims "
          "it. `rune doc lab::logger` reads the copy from one registry; "
          "`--no-open` only says where the page was written."),

        H("Which registry"),
        P("Every registry has a name: the one its `index.toml` declares, "
          "which `rune pkg init` takes from the directory or `--name`. A "
          "client adds a registry under that name, or under an alias of its "
          "own with `--name`, and the name is how the two are told apart "
          "from then on. Once added, a registry stays added — "
          "`~/.rune/registries.toml` keeps it for every project on the "
          "machine — until `rune registry remove` drops it."),
        SH("""$ rune registry add http://packages.example.org
● Added registry 'example' at http://packages.example.org (214 releases)
$ rune registry add file:///Volumes/shared/lab --name lab
● Added registry 'lab' at file:///Volumes/shared/lab (3 releases)
  ─  note: it calls itself 'research-lab'; here it is `lab`, as in `rune add lab::<package>`
$ rune registry list
example  http://packages.example.org    214 releases
lab      file:///Volumes/shared/lab     3 releases  calls itself 'research-lab'
$ rune registry remove lab
● Removed registry 'lab' (file:///Volumes/shared/lab)"""),
        P("Two registries may both have a `logger`. Left to itself, `rune add "
          "logger` takes the newest version any of them offers; "
          "`rune add lab::logger` takes it from `lab` alone, and the manifest "
          "records the choice as `logger = { version = \"1.0\", registry = "
          "\"lab\" }`, so a build anywhere makes the same one. A package "
          "that names a registry for one of its own dependencies carries "
          "that into the index — `dependencies = [\"lab::logger 1.0\"]` — and "
          "the resolver honours it transitively. Two askers naming different "
          "registries for one package is a conflict, reported like any "
          "other: one installed copy cannot come from both."),
        T(["Spelling", "Means"],
          [["`rune add logger`", "from whichever registry has the newest version"],
           ["`rune add lab::logger`, `rune add --registry lab logger`", "from `lab`; recorded in the manifest"],
           ["`rune search lab::.`, `rune search --registry lab`", "only what `lab` has"],
           ["`rune desc logger`", "the newest anywhere, and `also in:` the others"],
           ["`rune desc ecosystem::logger`", "that registry's copy, its versions alone"],
           ["`rune installed --registry lab`", "what came from `lab`"],
           ["`rune update --registry lab`", "move only what came from `lab`"],
           ["`rune deps --registry lab`", "the pinned packages that came from `lab`"],
           ["`rune doc lab::logger`", "the documentation of `lab`'s copy"]]),
        SH("""$ rune desc logger
logger v1.0.0
  The leveled logger, as the lab ships it: a Trace level below Debug, and a summary of what got through
  registry:     lab (file:///work/registry-lab)
  versions:     1.0.0
  also in:      ecosystem (0.9.0) — `rune add ecosystem::logger` takes it from there
  …
$ rune add lab::logger
● Added logger "1.0.0" from lab to Rune.toml (v1.0.0 installed)
$ rune deps
dashboard v0.1.0
├─ logger v1.0.0 (1.0) [lab]
└─ report v1.2.0 (1.2.0) [ecosystem]
   …"""),
        N("A name has to fit in front of `::package`: letters, digits, `_`, "
          "`-` and `.`. A registry whose index declares none is refused — a "
          "registry must provide a name — and two registries cannot be known "
          "by the same one; alias the second with `--name`.", label="Names"),

        H("Making a registry"),
        P("A registry is a directory: an `index.toml` listing every release "
          "with its checksum and dependencies, and a `packages/` tree of plain "
          "`tar` archives. `rune pkg init` makes an empty one; "
          "`--addPackage` packs a project — `Rune.toml`, `src/`, `docs/`, "
          "`tests/`, its C sources, its README and LICENSE, never `target/` — "
          "and adds the release to the index. The version comes from the "
          "manifest, and a version already there is refused unless "
          "`--force` says to replace it."),
        SH("""$ rune pkg init registry --name ecosystem
● Created registry 'ecosystem' in /work/registry
$ rune registry --addPackage ../geometry --dir registry
○ Packing geometry v0.3.0
● Added geometry v0.3.0 (7.5 KB, sha256 2980aae944e4…)
$ rune registry --addPackage ../shapes --dir registry
$ cat registry/index.toml
[registry]
name = "ecosystem"
format = 1
updated = "2026-09-11T10:16:41Z"

[[release]]
name = "geometry"
version = "0.3.0"
description = "Points and vectors on the plane: distances, dot products, and the arithmetic between them"
authors = ["The Rune examples"]
license = "MIT"
archive = "packages/geometry/geometry-0.3.0.tar"
sha256 = "2980aae944e4401d744e9f9914d3119a344a968be979b125b0d5b5e74ba982cc"
size = 7680
added = "2026-09-11T10:16:41Z"
dependencies = []

[[release]]
name = "shapes"
version = "1.0.0"
…
dependencies = ["geometry 0.3"]"""),
        P("Because it is files, there are three ways to make it reachable, and "
          "they need nothing in common:"),
        T(["Reach it as", "Set up with", "Good for"],
          [["a directory", "`rune registry add /shared/registry`", "a team on one machine or a shared drive"],
           ["`file://…`", "`rune registry add file:///shared/registry`", "the same, spelled as a URL"],
           ["`http://host:port`", "`rune registry --serve --dir registry --port 7878`, then `rune registry add http://host:7878`", "a network; any static web server works too"]],
          caption="Mirroring a registry is copying the directory."),
        SH("""$ rune registry --serve --dir registry
● Serving /work/registry at http://localhost:7878/
  ─  note: add it to a client with `rune registry add http://<this host>:7878`; Ctrl-C stops it

$ rune registry add http://localhost:7878
● Added registry 'ecosystem' at http://localhost:7878 (2 releases)
● this registry is not monitored: nothing here reviews what it serves, so read a package before you depend on it
$ rune search 'geo|shape'
geometry  v0.3.0     Points and vectors on the plane: distances, dot products, and the arithmetic between them
shapes    v1.0.0     Circles, rectangles and polygons over geometry: area, perimeter, containment and bounding boxes"""),
        P("The server is a static file server with a front page listing what "
          "it holds; its `-v` prints each request. A client keeps the "
          "registries it uses in `~/.rune/registries.toml` — by name, URL, "
          "and the name the registry declares when an alias differs — caches "
          "each index under `~/.rune/cache/`, and works from the cached copy "
          "when a registry cannot be reached."),

        H("Releasing a new version"),
        P("Bump `version` in the manifest, make the change, and add the "
          "package again: the index gains a release and keeps the old one, so "
          "a project pinned to it goes on building. `examples/package/` keeps "
          "one directory per released version — `packages/stats/2.0.0/`, "
          "`packages/stats/2.1.0/` — and rebuilds the whole registry from "
          "them with `python3 ecosystem.py build`."),
        SH("""$ sed -i 's/^version = "2.0.0"/version = "2.1.0"/' stats/Rune.toml
$ rune registry --addPackage stats --dir registry
● Added stats v2.1.0 (9.0 KB, sha256 d00426eaff5b…)
$ cd ../dashboard && rune update
○ Fetching stats v2.1.0
● Installed stats v2.1.0
● stats: v2.0.0 -> v2.1.0
● 1 installed package version is not used by any project; `rune remove` with no arguments uninstalls it"""),
        N("Every install checks the archive against the checksum the index "
          "recorded, which catches a corrupted or tampered file. It does not "
          "vouch for the package: a registry added with `rune registry "
          "add` is whoever runs it, and nothing reviews what it serves. Read "
          "what you depend on, and keep `Rune.lock` so what you read is what "
          "you build.", label="Trust", tone="warn"),

        H("The example ecosystem"),
        P("`examples/package/` has seven packages — `units` and `stats` in two "
          "versions each, `geometry`, `logger`, `shapes`, `plot` and "
          "`report`, with a diamond through `stats` — two programs, two "
          "registries built from them (`ecosystem`, and `lab` with its own "
          "`logger 1.0.0` that `dashboard` asks for by name), and a script "
          "that builds them, serves them, or proves the whole cycle under a "
          "temporary `RUNE_HOME`. It is also the CTest test `rune_ecosystem`."),
        SH("""$ cd examples/package
$ python3 ecosystem.py check
== building the registries
● Added geometry v0.3.0 (7.5 KB, sha256 …)
…
== adding them to a fresh client
● Added registry 'ecosystem' at file:///…/registry (9 releases)
● Added registry 'lab' at file:///…/registry-lab (1 release)
== every package's tests, against dependencies from the registries
  ecosystem::geometry 0.3.0: ● Test result: 1 file(s) passed, 0 failed
  ecosystem::report 1.2.0:   ● Test result: 1 file(s) passed, 0 failed
  lab::logger 1.0.0:         ● Test result: 1 file(s) passed, 0 failed
  …
== the apps
-- dashboard: rune run
   Shapes
   ======
   1. Circles
      circle r=1.0 at (0.0, 0.0)
      …
-- dashboard: rune deps
   dashboard v0.1.0
   ├─ logger v1.0.0 (1.0) [lab]
   ├─ report v1.2.0 (1.2.0) [ecosystem]
   …
== what is installed, and who uses it
geometry v0.3.0  4 projects  [ecosystem]
logger v1.0.0    1 project   [lab]
stats v2.1.0     4 projects  [ecosystem]
units v1.0.0     1 project   [ecosystem]
units v1.1.0     2 projects  [ecosystem]
ecosystem: every package tests clean and both apps run"""),
    ],
    keywords=["package", "registry", "publish", "release", "version",
              "semver", "Rune.toml", "Rune.lock", "rune add", "rune pkg",
              "rune search", "dependencies", "ecosystem", "install",
              "registry", "addPackage", "serve", "registry name", "alias",
              "--registry", "registry::package", "rune doc", "server list",
              "server remove"]))


# ===========================================================================
# Concurrency
# ===========================================================================
SECTIONS.append(Sec(
    "concurrency", "abstraction", "Threads and sharing",
    "`std::thread` runs more than one thing at once. The rule the whole "
    "module rests on is that **threads share nothing they can change** — and "
    "the compiler, not the programmer, is what checks it.",
    [
        H("Two questions"),
        P("Every value that reaches a thread is asked one of two things, the "
          "pair Rust and Swift settled on:"),
        T(["Mark", "Asks"],
          [["`Send`", "may a value of this type *move* to another thread?"],
           ["`Sync`", "may one be *reached from* several at once?"]]),
        P("Neither is declared. The compiler reads both off the type, the way "
          "it reads whether a type is reference counted, so they stay true as "
          "a program changes instead of drifting out of date. A struct of "
          "numbers is `Send` because of what it is; adding a class field "
          "takes it away, at the point of the change rather than later."),
        T(["Type", "Send", "Sync", "Why"],
          [["`i64`, `f64`, `bool`, `Character`", "yes", "yes",
            "there is nothing to share"],
           ["`String`", "yes", "yes",
            "counted, but its contents never change and the count is atomic"],
           ["struct, tuple, array, enum", "if its parts are", "if its parts "
            "are", "an aggregate is whatever it holds"],
           ["class", "no", "no",
            "a reference to something any holder can write to"],
           ["closure", "no", "no", "carries the values it captured"],
           ["`*T`, `Any`, `dyn Mark`", "no", "no",
            "the compiler cannot see what they reach"],
           ["`thread::Mutex<T>`", "yes", "yes",
            "it synchronises its own access, and says so"]]),
        N("A class is a *shared, mutable* reference: `let` fixes the binding, "
          "not the object, so any holder can write to its fields. That is why "
          "no ordinary class is `Send`, and why `Mutex` is not a convenience "
          "but the way.",
          label="Why a class is neither", tone="warn"),

        H("Running something"),
        P("`spawn` takes a top-level `fn` and one argument, and hands back a "
          "handle to join. The entry is a `fn` rather than a closure because "
          "a closure carries what it captured, and sharing that is the thing "
          "this module exists to prevent — so what the thread needs, it is "
          "given."),
        S('''import std::io
import std::thread

/// Sums `from..to`. The bounds arrive as a tuple, which crosses as happily
/// as a number would: what matters is what it is made of, and these are
/// two `i64`.
fn sumRange(bounds: (i64, i64)) -> i64 {
    var total = 0
    var i = bounds.0
    while i <= bounds.1 { total += i; i += 1 }
    total
}

fn shout(name: String) -> String { name + "!" }

fn main() -> i64 {
    // One sum, split down the middle and done at the same time. The halves
    // add up to what doing it in one go would have given.
    var lower = thread::spawn(sumRange, (1, 500000))
    var upper = thread::spawn(sumRange, (500001, 1000000))
    println!("in two: {}", lower.join() + upper.join())
    println!("in one: {}", sumRange((1, 1000000)))

    // A String crosses in, and another comes back.
    var greeting = thread::spawn(shout, "hello" + " world")
    println!("{}", greeting.join())

    println!("this machine runs {} at once", thread::hardwareThreads())
    0
}''', mode="run", title="Fork and join"),
        N("A handle that goes out of scope without being joined waits for its "
          "thread anyway. Detaching instead would leave the task's memory "
          "with nobody to free it, and a leak is a worse default than a wait.",
          label="Dropping a handle"),

        H("When it will not compile"),
        P("The interesting part is what `spawn` refuses. The bound is an "
          "ordinary one, so the error arrives at the call that tried it and "
          "says what to do instead:"),
        S('''import std::thread

class Counter {
    var n: i64
    fn init(self, n: i64) { self.n = n }
}

fn bump(c: Counter) -> i64 { c.n += 1; c.n }

fn main() -> i64 {
    var h = thread::spawn(bump, Counter(0))
    h.join()
    0
}''', mode="diag", title="A class cannot cross"),
        P("`Send` cannot be bound by hand either. It is an answer, not a "
          "promise, and letting one be written would put the whole guarantee "
          "behind a line nobody has to justify."),

        H("Shared mutable state"),
        P("`Mutex<T>` is the one type that is `Sync` while holding something "
          "that is not — because it is the one type that synchronises access "
          "to what it holds. It says so with `@sync(\"reason\")`, which is "
          "the single place the compiler takes such a claim on trust."),
        S('''import std::io
import std::thread

fn addOne(n: i64) -> i64 { n + 1 }

/// Four of these run at once against the same counter.
fn bumpAThousand(shared: thread::Mutex<i64>) -> i64 {
    var i = 0
    while i < 1000 { shared.withLock(addOne); i += 1 }
    0
}

fn main() -> i64 {
    let total = thread::Mutex<i64>(0)

    var a = thread::spawn(bumpAThousand, total)
    var b = thread::spawn(bumpAThousand, total)
    var c = thread::spawn(bumpAThousand, total)
    var d = thread::spawn(bumpAThousand, total)
    a.join(); b.join(); c.join(); d.join()

    println!("{}", total.get())
    0
}''', mode="run", title="Four threads, one counter"),
        P("`withLock` takes a `fn` from the value to what it should become, "
          "and holds the lock for exactly as long as that runs. There is no "
          "way to keep a reference to what is inside past the moment the lock "
          "is released, because none is ever handed out."),
        T(["Method", "Does"],
          [["`withLock(f)`", "replaces the value with `f(value)`, locked"],
           ["`get()`", "a copy of the value, read locked"],
           ["`set(v)`", "replaces the value, locked"]]),

        H("Asking directly"),
        P("The bound is what belongs in a signature, because it explains "
          "itself when it fails. `std::reflect` asks the same question where "
          "code wants to take a different path rather than refuse."),
        S('''import std::reflect

struct Pair { a: i64, b: String }

class Counter {
    var n: i64
    fn init(self, n: i64) { self.n = n }
}

struct Holder { c: Counter }

fn main() -> i64 {
    println!("{} {} {}", reflect::isSend<i64>(), reflect::isSend<String>(),
             reflect::isSend<Pair>())
    // A class, and anything that holds one.
    println!("{} {}", reflect::isSend<Counter>(), reflect::isSend<Holder>())
    0
}''', mode="run", title="Send, as a question"),

        H("Sharing a value"),
        P("`Arc<T>` is one value several threads may read. A class already is "
          "a shared reference, but a *mutable* one, which is why no class is "
          "`Send`; `Arc` is the immutable counterpart — what it holds is set "
          "when it is made, and everything after that is a read. It is "
          "`Sync` when `T` is, since an `Arc` around something writable "
          "would only move the problem down a level."),
        S('''import std::io
import std::thread

fn total(shared: thread::Arc<[4:i64]>) -> i64 {
    var sum = 0
    for v in shared.get() { sum += v }
    sum
}

fn main() -> i64 {
    let table = thread::Arc<[4:i64]>([2, 3, 5, 7])
    var here = thread::spawn(total, table)
    var there = thread::spawn(total, table)
    println!("{} and the original still has {} entries",
             here.join() + there.join(), table.get().$length())
    0
}''', mode="run", title="One value, two readers"),
        T(["Want", "Reach for"],
          [["read the same value from several threads", "`Arc<T>`"],
           ["change the same value from several threads", "`Mutex<T>`"],
           ["hand values from one thread to another", "`Channel<T>`"]]),

        H("Channels"),
        P("A `Channel<T>` is a queue one thread puts values into and another "
          "takes them out of. `receive` waits until there is something to "
          "take, or until the channel is closed and empty — which is how a "
          "consumer knows to stop. The queue grows as needed, so `send` never "
          "waits."),
        S('''import std::io
import std::thread

/// Takes values until the channel is closed and empty.
fn consume(line: thread::Channel<i64>) -> i64 {
    var total = 0
    while true {
        match line.receive() {
            Some(v) => total += v,
            None => break,
        }
    }
    total
}

fn main() -> i64 {
    // Three consumers on one queue. Each takes what it can, and the three
    // totals add up to the whole.
    let jobs = thread::Channel<i64>()
    var a = thread::spawn(consume, jobs)
    var b = thread::spawn(consume, jobs)
    var c = thread::spawn(consume, jobs)

    var i = 1
    while i <= 1000 { jobs.send(i); i += 1 }
    jobs.close()               // no more coming; wake everyone waiting

    println!("{}", a.join() + b.join() + c.join())
    0
}''', mode="run", title="One queue, three consumers"),
        T(["Method", "Does"],
          [["`send(v)`", "puts `v` at the back and wakes a receiver"],
           ["`receive()`", "the next value, waiting; `nil` once closed and "
            "empty"],
           ["`tryReceive()`", "a value if one is already there, never waits"],
           ["`close()`", "says no more will be sent, and wakes every waiter"],
           ["`pending()`", "how many are waiting to be taken"]]),
        N("Sending to a closed channel is a panic rather than a value that "
          "quietly vanishes. Closing twice is harmless: the second says "
          "nothing the first did not.",
          label="After close"),

        H("Counters and flags"),
        P("A `Mutex` is the general answer, and a heavy one. For a counter or "
          "a flag there is a cheaper one: `std::atomic` wraps a single number "
          "in operations the machine performs in one indivisible step, so no "
          "two threads can lose an update between them and none of them ever "
          "waits."),
        S('''import std::io
import std::atomic
import std::thread

fn bump(counter: atomic::Counter) -> i64 {
    var i = 0
    while i < 100000 { counter.increment(); i += 1 }
    0
}

/// `raise` sets the flag and says whether *this* call was the one that did,
/// so exactly one caller out of any number gets `true`.
fn tryClaim(flag: atomic::Flag) -> i64 {
    if flag.raise() { return 1 }
    0
}

fn main() -> i64 {
    let served = atomic::Counter(0)
    var a = thread::spawn(bump, served)
    var b = thread::spawn(bump, served)
    var c = thread::spawn(bump, served)
    var d = thread::spawn(bump, served)
    a.join(); b.join(); c.join(); d.join()
    println!("{}", served.load())

    let once = atomic::Flag(false)
    var w = thread::spawn(tryClaim, once)
    var x = thread::spawn(tryClaim, once)
    var y = thread::spawn(tryClaim, once)
    println!("winners: {}", w.join() + x.join() + y.join())
    0
}''', mode="run", title="Four threads, one counter, no lock"),
        T(["Method", "Does"],
          [["`load()` / `store(v)`", "read, write"],
           ["`add(d)` / `sub(d)`", "and hand back the value *before*"],
           ["`increment()` / `decrement()` / `next()`", "by one"],
           ["`exchange(v)`", "replace, handing back what was there"],
           ["`compareExchange(was, want)`",
            "store `want` only while the value is still `was`"],
           ["`update(f)`", "apply `f`, retrying until it sticks"],
           ["`raiseTo(n)`", "keep the larger of the two"],
           ["`Flag::raise()`", "set it, and say whether this call did"],
           ["`Flag::lower()` / `isRaised()`", "clear it, read it"]]),
        P("Returning the value from *before* is what makes `add` a hand-out "
          "rather than a count: every caller gets a number nobody else got. "
          "`compareExchange` is how anything more involved is built — read, "
          "work out the new value, and swap it in only if nobody moved it "
          "meanwhile; `update` is that loop, written once."),
        N("Two atomic operations are still two moments. A counter is safe "
          "because it *is* one number; code that has to change two things "
          "together needs a `Mutex`, whatever the pieces are made of.",
          label="One number at a time", tone="warn"),
        P("The cost is the point. Four threads doing 400,000 increments each, "
          "the same work both ways:"),
        SH("""atomic: 1600000 = 27ms
mutex:  1600000 = 120ms"""),

        H("Waiting"),
        P("`std::time` holds a length of time as a count of nanoseconds. A "
          "duration is built by naming its unit, because a bare number is how "
          "the wrong unit gets passed."),
        S('''import std::io
import std::thread
import std::time

fn main() -> i64 {
    let d = time::milliseconds(1500)
    println!("{} is {} ms, or {} whole seconds", d, d.asMilliseconds(),
             d.asSeconds())
    println!("{} {} {}", time::seconds(2), time::minutes(3),
             time::microseconds(5))
    println!("{}", time::milliseconds(250) + time::milliseconds(750))

    // A clock that only goes forwards, for measuring how long something took.
    let start = time::now()
    thread::sleep(time::milliseconds(50))
    println!("waited at least 50ms: {}", start.elapsed().asMilliseconds() >= 50)
    0
}''', mode="run", title="Durations and a clock"),
        P("`sleep` stops the thread for *at least* that long — the operating "
          "system decides when to wake it, and it will not be early. An "
          "`Instant` from `time::now()` means nothing on its own; what it is "
          "for is `since` and `elapsed`."),

        H("What reference counting costs"),
        P("A count two threads may touch has to be changed in one indivisible "
          "step. Doing that to *every* count would be the simple answer, and "
          "it is what Swift does — but measured on a loop that does little "
          "besides retain and release, it costs about twice what an ordinary "
          "add does."),
        P("So the compiler picks from the static type instead. `String`, "
          "`Arc` and anything marked `@sync` — the types that can actually be "
          "reached from two threads — use an atomic pair; everything else "
          "uses a plain one. Nothing branches at run time, and a class that "
          "cannot be shared pays nothing for the fact that some other type "
          "can."),
        T(["Counted with", "Which types"],
          [["an ordinary add", "classes, `Any`, `dyn Mark` — none of which "
            "is `Send`"],
           ["an atomic add", "`String`, `Arc<T>`, `Mutex<T>`, any `@sync` "
            "type — and closures, because `task::offload` hands one to a "
            "worker thread while the thread that made it may still be "
            "letting go of its own reference"]]),
        P("The weak-reference table is the one piece of runtime state every "
          "thread shares, and it has a lock of its own. Everything else the "
          "runtime keeps is per-object."),

        H("Where this works"),
        P("The threading layer has two backings: pthreads, and the Win32 "
          "calls on Windows. The pthreads one is what every example on this "
          "page runs on, and what the test suite exercises."),
        T(["Platform", "State"],
          [["macOS, Linux, and anything with pthreads", "works, and is what "
            "the tests run against"],
           ["Windows", "**not yet**: a single `spawn`, several concurrent "
            "ones, and `Mutex` all work, but destroying a handle and "
            "spawning again faults. Everything else cross-compiles and runs; "
            "only threads are affected"]]),
        N("Said plainly because a reference that hides its edges wastes your "
          "time: if you are targeting Windows, do not use `std::thread` yet. "
          "The rest of the language is unaffected.",
          label="Windows", tone="warn"),

        H("What is not here"),
        T(["Missing", "Instead"],
          [["a closure as a thread entry", "a top-level `fn` and an argument"],
           ["a bounded channel", "`Channel` grows; `send` never blocks"],
           ["`select` over several channels", "one channel, or a thread "
            "for each"],
           ["read/write locks, semaphores", "`Mutex` only"],
           ["thread-local storage", "nothing; a `global var` is shared, and "
            "reaching one from two threads is a race the compiler does not "
            "yet catch"]]),
        N("That last row is the sharp edge worth knowing: `Send` and `Sync` "
          "check what *crosses*, and a `global var` crosses nothing — it is "
          "simply already there. Keep globals immutable in a program that "
          "starts threads.",
          label="Globals are not checked", tone="warn"),
        P("For several things in progress on *one* thread — waiting on each "
          "other rather than running at once — see **Tasks and futures**: "
          "`async fn`, `.await`, and `std::task`."),
    ],
    keywords=["thread", "threads", "concurrency", "parallel", "spawn", "join",
              "Send", "Sync", "Mutex", "lock", "shared", "race", "atomic",
              "sync", "hardwareThreads", "handle"]))


# ===========================================================================
# Tasks and futures
# ===========================================================================
SECTIONS.append(Sec(
    "tasks", "abstraction", "Tasks and futures",
    "`async fn` and `.await`: one thread doing several things at once. Where "
    "threads run *at the same time* and the compiler checks what may cross "
    "between them, tasks take turns on one thread — so they share whatever "
    "they like, and the question is only who runs next.",
    [
        H("A task is a stack"),
        P("Say what the mechanism is first, because everything else follows "
          "from it. A task is a piece of code with a stack of its own. "
          "`.await` on something that is not finished saves that stack and "
          "switches to another; whatever finishes it switches back. That is "
          "the whole of it. Nothing is rewritten into a state machine, so "
          "everything a function can do, an `async fn` can do — `defer`, "
          "`?`, loops, recursion through `.await` — and a task looks in a "
          "traceback like what it is: a call, parked."),
        P("An `async fn` is an ordinary function whose body runs as a task. "
          "Calling it starts the task and hands back a `Future<T>`, the "
          "promise of a `T`; `.await` collects the result, parking the task "
          "that asked until it is there and letting the others run "
          "meanwhile."),
        S('''import std::io
import std::task
import std::time

async fn step(name: String, delay: i64) -> String {
    io::println(name + " starts")
    task::sleep(time::milliseconds(delay)).await
    io::println(name + " ends")
    name + "!"
}

async fn main() -> i64 {
    let a = step("a", 20)          // starts now, runs until its sleep
    let b = step("b", 5)           // so does this
    io::println("main between")
    io::println(a.await + " " + b.await)
    0
}''', mode="run", title="Two tasks, taking turns"),
        P("Both tasks start at their call and run until they first have to "
          "wait; `main` carries on in between; the shorter sleep finishes "
          "first. Nothing here ran at the same time as anything else — the "
          "three took turns — which is why `step` could have written to a "
          "class the other held with no lock at all."),
        N("The rewrite the compiler does is small enough to show. "
          "`async fn f(a: A) -> T { body }` becomes "
          "`fn f(a: A) -> task::Future<T> { task::spawn(move ||() -> T { body }) }`: "
          "the parameters are captured into a closure, and `spawn` starts it "
          "on a stack of its own. Generics, libraries and the type system "
          "see an ordinary function whose result is a `Future`.",
          label="What `async` means"),

        H("Where `.await` may be written"),
        P("`.await` parks the task it is in, so it needs one: it is allowed "
          "directly inside an `async fn`, an `async ||` closure or an "
          "`async { }` block, and nowhere else — not in a plain closure "
          "written inside one, which is a function of its own and may be "
          "called from anywhere."),
        S('''import std::task

async fn answer() -> i64 { 42 }

fn main() -> i64 {
    let n = answer().await
    n - 42
}''', mode="diag", title="Not from ordinary code"),
        P("From ordinary code the bridge is `wait()`, which blocks the thread "
          "until the future is done — running every other task meanwhile — "
          "or `task::run`, the same thing spelled for a `main`. An "
          "`async fn main` is that, written for you: the task runs to the "
          "end and its result is the exit code."),
        S('''import std::io
import std::task

async fn answer() -> i64 { 6 * 7 }

fn main() -> i64 {
    io::println(answer().wait())
    io::println(task::run(answer()))
    0
}''', mode="run", title="`wait()` and `run`"),
        P("A future may be awaited more than once; each asking gets the value "
          "again. Inside a task `wait()` does exactly what `.await` does — "
          "parks the task — so calling a function that waits is never a "
          "thread blocked by mistake. The keyword is there for the reader "
          "and the compiler: it marks where a task can be set aside."),
        P("Postfix, and bare, because it chains: `fetch(url).await?` reads "
          "the result and then propagates its error, and `client.get(id)"
          ".await.length()` needs no parentheses."),

        H("Blocks and closures"),
        P("`task::spawn` starts a closure as a task. An `async { }` block is "
          "the same thing written in place — its value is the future — and "
          "an `async ||(...)` closure starts a task each time it is called."),
        S('''import std::io
import std::task

fn main() -> i64 {
    let work = task::spawn(||() -> i64 { 3 + 4 })
    io::println(work.wait())

    let base = 5
    let block = async { base + 3 }
    io::println(block.wait())

    let scale = async ||(x: i64) -> i64 { x * 5 }
    io::println(scale(2).wait())
    0
}''', mode="run", title="Three ways to start one"),
        P("A block or closure captures what it uses by value, as every closure "
          "does: the task gets its own copy, made when it starts."),

        H("What a task may take"),
        P("The body runs after the call that started it has returned — the "
          "caller may have moved on, returned, dropped its locals — so every "
          "parameter is captured into the task by value. A shared borrow of "
          "a class, `&Counter`, is the handle itself, kept alive by the "
          "capture, and is fine. A `&var` of anything, or a `&` of a value "
          "type, would point at a slot the caller has left, and is refused "
          "as `E0284`."),
        S('''import std::task

async fn bump(count: &var i64) { *count += 1 }

fn main() -> i64 { 0 }''', mode="diag", title="A borrow that would dangle"),
        P("The same rule decides what `self` an `async` method may take: a "
          "class's `&self` or `&var self` is the object, and is fine; a "
          "struct's would be a pointer into the caller's slot, so an `async` "
          "method on a struct or an enum takes `self` by value."),
        S('''import std::io
import std::task
import std::time

class Store {
    var prefix: String
    var count: i64
    fn init(self, prefix: String) { self.prefix = prefix; self.count = 0 }

    pub async fn load(&var self, id: i64) -> String {
        task::sleep(time::milliseconds(1)).await
        self.count += 1
        self.prefix + id.$str()
    }
}

async fn main() -> i64 {
    var store = Store("item")
    let a = store.load(1)
    let b = store.load(2)
    io::println(a.await + " " + b.await + " count=" + store.count.$str())
    0
}''', mode="run", title="An async method"),
        P("`async fn` goes wherever `fn` goes: in a class, a struct, an "
          "`extend`, a mark's requirements and the `bind` that supplies "
          "them, a library's public interface. It cannot be an `init` or a "
          "`deinit`, which have to finish before the object exists and "
          "before it is gone."),

        H("Sharing between tasks"),
        P("Tasks on one thread take turns and never overlap, so the two "
          "questions `std::thread` asks — `Send`, `Sync` — are not asked "
          "here. Two tasks may hold one class, and both may write to it; a "
          "write is finished before the other task gets a turn."),
        S('''import std::io
import std::task

class Counter {
    var hits: i64
    fn init(self) { self.hits = 0 }
    fn bump(&var self) { self.hits += 1 }
}

async fn touch(c: Counter, times: i64) {
    var i = 0
    while i < times {
        c.bump()
        task::yieldNow()          // let the other task have a turn
        i += 1
    }
}

async fn main() -> i64 {
    let c = Counter()
    let first = touch(c, 3)
    let second = touch(c, 3)
    first.await
    second.await
    io::println(c.hits)
    0
}''', mode="run", title="One class, two tasks, no lock"),
        P("Under `--memory zombie` a value has one owner, so the tasks share "
          "by borrowing — `touch(c: &Counter, ...)` — and change what they "
          "share through `mem::Checked<T>`, exactly as two borrows anywhere "
          "else would. A future's result is cloned out to each awaiter, so "
          "the future keeps its own and frees it exactly once. The future "
          "itself is a handle: `$clone()` — and so a read out of a "
          "`Vector<Future<T>>`, or a `vec![...]` of them — shares the task "
          "rather than copying what is behind it."),
        N("A task belongs to the thread that made it, and futures do not "
          "cross threads: awaiting one from another thread is a panic. Each "
          "thread that uses tasks has an executor of its own.",
          label="One thread, one executor"),

        H("Sleeping, waiting, and letting others run"),
        T(["Call", "Does"],
          [["`task::sleep(duration).await`", "parks this task until the time "
            "has passed; the thread runs the others"],
           ["`task::yieldNow()`", "lets every task that is ready run before "
            "this one continues"],
           ["`task::all(futures).await`", "every result, in order, once every "
            "future in the `Vector` is done"],
           ["`task::first(futures).await`", "the first to finish: its index "
            "and the future itself, already done. The lowest index when "
            "several are"],
           ["`task::race(futures).await`", "the value of the first to finish"],
           ["`task::pending<T>()`", "a future with no task behind it, which "
            "`complete(value)` finishes — how a callback becomes something "
            "awaitable"],
           ["`future.isDone()`", "whether the result is there"]]),
        S('''import std::io
import std::task
import std::time
import std::collections::vector

async fn fetch(id: i64) -> String {
    task::sleep(time::milliseconds(3 - id)).await
    "item " + id.$str()
}

async fn main() -> i64 {
    let results = task::all(vec![fetch(1), fetch(2), fetch(3)]).await
    for r in results { io::println(r) }

    let answer = task::pending<i64>()
    let doubled = async { answer.await * 2 }
    io::println(doubled.isDone())
    answer.complete(21)
    io::println(doubled.await)
    0
}''', mode="run", title="`all` and `pending`"),
        S('''import std::io
import std::task
import std::time
import std::collections::vector

async fn mirror(name: String, delay: i64) -> String {
    task::sleep(time::milliseconds(delay)).await
    "from " + name
}

async fn main() -> i64 {
    let slow = mirror("slow", 30)
    let fast = mirror("fast", 5)
    let (which, winner) = task::first(vec![slow, fast]).await
    io::println(which)
    io::println(winner.await)
    // The loser keeps running; nothing cancels it. Here it is collected.
    io::println(slow.await)
    io::println(task::race(vec![mirror("a", 20), mirror("b", 3)]).await)
    0
}''', mode="run", title="`first` and `race`"),
        P("`first` hands back the winner rather than only its value, so the "
          "caller knows which it was and can await the others later; it "
          "cancels nothing. `race` is `first` with the rest cancelled — and "
          "waited for, so that when the value comes back nothing of the race "
          "is still running. See **Cancellation and timeouts**."),

        H("Cancellation and timeouts"),
        P("A task can be asked to stop. `cancel` marks it; at its next "
          "*suspension point* — an `.await`, a `sleep`, a `yieldNow`, a "
          "`checkpoint` — the task leaves its body the way `?` leaves a "
          "function: from that line, running its `defer`s and releasing "
          "what it holds on the way out, and it ends without a result. A "
          "task parked at one of those points is woken to leave at once; "
          "one that is running leaves when it next reaches one. Nothing is "
          "interrupted mid-statement, which is what makes a cancelled task "
          "safe to reason about: every invariant it keeps between "
          "suspension points still holds."),
        S('''import std::io
import std::task
import std::time

async fn slow(name: String, ms: i64) -> String {
    defer io::println(name + " leaves")
    task::sleep(time::milliseconds(ms)).await
    io::println(name + " finished")
    name.$clone()
}

async fn main() -> i64 {
    let t = slow("tortoise", 40)
    task::sleep(time::milliseconds(5)).await
    t.cancel()
    match t.outcome() {
        task::Outcome::Done(v) => io::println("done " + v),
        task::Outcome::Cancelled => io::println("cancelled, done=" + t.isDone().$str()),
    }
    0
}''', mode="run", title="A task cancelled at its sleep"),
        P("The tortoise left at its `sleep`: its `defer` ran, and \"finished\" "
          "never printed. A cancelled task has no result, so `.await` on it "
          "is a panic; `outcome()` waits like `.await` and says which of "
          "the two happened. `isCancelled()` says whether a cancel has been "
          "asked for, whether or not the task has reached the point where "
          "it leaves; a task that never suspends again finishes with its "
          "value regardless."),
        T(["Call", "Does"],
          [["`future.cancel()`", "asks the task to stop at its next "
            "suspension point; a timer, a socket wait or a `pending` future "
            "simply ends"],
           ["`future.outcome()`", "waits, then `Done(value)` or `Cancelled`"],
           ["`future.isCancelled()`", "whether a cancel has been asked for"],
           ["`task::checkpoint()`", "leaves the task here if it has been "
            "cancelled — for a loop with no `.await` in it"],
           ["`task::race(futures).await`", "the first value; the rest are "
            "cancelled the moment it arrives, and have left before it is "
            "handed back"],
           ["`task::timeout(limit, future).await`", "`Some(value)` within "
            "`limit`, or `nil` — the task behind it cancelled, and gone"]]),
        S('''import std::io
import std::task
import std::time

async fn fetch(ms: i64) -> String {
    task::sleep(time::milliseconds(ms)).await
    "page"
}

async fn main() -> i64 {
    match task::timeout(time::milliseconds(10), fetch(60)).await {
        Some(page) => io::println(page),
        None => io::println("too slow"),
    }
    match task::timeout(time::milliseconds(60), fetch(5)).await {
        Some(page) => io::println(page),
        None => io::println("too slow"),
    }
    0
}''', mode="run", title="A deadline"),
        N("Cancellation is cooperative and not recursive. A task that computes "
          "without ever suspending is never interrupted — put a `checkpoint()` "
          "in its loop — and cancelling a task does not cancel the tasks it "
          "started; cancel those too if they should stop. A cancel reaches "
          "`wait()` only at the next `.await` after it: `wait()` is for code "
          "that is not `async`, and is not a suspension point.",
          label="What cancel cannot do", tone="warn"),

        H("Work on other threads"),
        P("Anything that would block — a slow read, a long computation — "
          "would stop every task on the thread if it ran there. `blocking` "
          "runs a `fn` on a worker thread and hands back a future that is "
          "done when it returns; `offload` does the same for a closure, and "
          "`offloadAsync` for an `async` closure. The workers are a pool of "
          "at most `task::workers()` threads — as many as the machine runs "
          "at once — started as they are needed and then kept. Each is an "
          "ordinary thread with an executor of its own, so a job may start "
          "tasks there and wait for them: tasks do run on several threads "
          "at once, one executor per thread, and only results cross back."),
        S('''import std::io
import std::task
import std::time

fn slowSquare(n: i64) -> i64 {
    var i = 0
    var noise = 0
    while i < 100000 { noise += (n * n) % 7; i += 1 }
    n * n
}

async fn compute(n: i64) -> i64 {
    task::sleep(time::milliseconds(1)).await
    n * 2
}

async fn main() -> i64 {
    let a = task::blocking(slowSquare, 12)
    let b = task::blocking(slowSquare, 13)
    io::println(a.await + b.await)

    let base = 100
    io::println(task::offload(||() -> i64 { base + 1 }).await)

    // The async closure's tasks run on the worker's executor.
    let total = task::offloadAsync(async ||() -> i64 {
        let x = compute(1)
        let y = compute(2)
        x.await + y.await
    })
    io::println(total.await)
    0
}''', mode="run", title="A pool of workers"),
        P("The rule is `thread::spawn`'s, because these are threads: what "
          "crosses has to be `Send`. For `blocking` that is the argument "
          "and the result. For a closure it is everything it captured — "
          "and a closure's *type* says nothing about its captures, so the "
          "check is made on the closure as written, at the call, which is "
          "why one has to be written there:"),
        S('''import std::task

class Counter { var n: i64
    fn init(self) { self.n = 0 } }

fn main() -> i64 {
    let c = Counter()
    task::offload(||() -> i64 { c.n + 1 }).wait()
}''', mode="diag", title="A capture that may not cross"),

        H("Sockets driven by tasks"),
        P("`std::net`'s `TcpStream` blocks: a `read` holds the thread until "
          "bytes arrive. `net::AsyncStream` and `net::AsyncListener` are the "
          "same sockets set not to wait: each `read`, `write` and `accept` "
          "tries at once and, if nothing is ready, parks the task on the "
          "socket — `task::readable`, `task::writable` — until it is, while "
          "the other tasks run. The executor watches every parked socket "
          "with `poll`, alongside its timers. Nothing about the bytes "
          "changes; only who waits, and how."),
        S('''import std::io
import std::net
import std::task
import std::collections::vector

async fn serve(server: net::AsyncListener, count: i64) -> i64 {
    var served = 0
    while served < count {
        match server.accept().await {
            Ok(conn) => { handle(conn); served += 1 },
            Err(e) => { io::println(net::describe(e)); return served },
        }
    }
    served
}

async fn handle(conn: net::AsyncStream) {
    var c = conn
    if c.read(1024).await is Ok(bytes) {
        c.writeText("echo: " + bytes.toString()).await
        c.shutdown(net::Shutdown::Write)
    }
}

async fn client(port: i32, message: String) -> String {
    match net::connectAsync("127.0.0.1", port).await {
        Ok(conn) => {
            var c = conn
            c.writeText(message).await
            c.shutdown(net::Shutdown::Write)
            c.readAll().await.unwrap()
        },
        Err(e) => net::describe(e),
    }
}

async fn main() -> i64 {
    var server = net::listenAsync("127.0.0.1", 0).unwrap()
    let port = server.port()
    let serving = serve(server, 2)
    for r in task::all(vec![client(port, "one"), client(port, "two")]).await {
        io::println(r)
    }
    io::println(serving.await)
    0
}''', mode="run", title="An echo server and its clients, on one thread"),
        P("Resolving a name and connecting both wait on the network, so "
          "`connectAsync` does that part on a worker and parks the task "
          "until it is done. Files are not sockets: an operating system "
          "cannot say a regular file is \"ready\", so file I/O goes through "
          "`offload`, as it does in every runtime of this kind."),

        H("What it costs"),
        T(["Thing", "Cost"],
          [["starting a task", "a stack from a per-thread pool, a small "
            "box, and two switches — about half a microsecond in all"],
           ["`.await` on something finished", "a check"],
           ["`.await` on something not", "two switches of a few dozen "
            "instructions each"],
           ["a task's stack", "`task::stackSize()` bytes of address space "
            "(1 MiB unless changed), *reserved* — a task that touches 20 KB "
            "of it costs 20 KB"],
           ["a task that never finishes", "its stack and whatever it holds, "
            "until the program ends; the exit report counts them"]]),
        P("A task that runs off the end of its stack faults rather than "
          "writing over whatever lies beyond it; a deep recursion inside a "
          "task wants `task::setStackSize` raised first."),
        P("Waiting on a future that nothing can finish — no task ready, no "
          "timer pending, no thread working — is reported as a deadlock "
          "rather than hung:"),
        S('''import std::io
import std::task

async fn main() -> i64 {
    let never = task::pending<i64>()
    io::println("waiting")
    never.await
}''', mode="panic", title="Reported, not hung"),

        H("What is not here"),
        T(["Missing", "Instead"],
          [["a task moving between threads",
            "a task stays on the thread that made it; `offloadAsync` "
            "starts one on a worker, and results cross back"],
           ["pre-emptive cancellation",
            "cooperative: a `checkpoint()` in a loop that never suspends"],
           ["asynchronous file I/O",
            "`offload` around the call; a file is never \"not ready\""],
           ["`select` over channels", "`first` over futures; a `pending` "
            "future a channel's reader completes"]]),
    ],
    keywords=["async", "await", "task", "tasks", "future", "futures",
              "spawn", "sleep", "blocking", "offload", "pending", "all",
              "first", "race", "timeout", "cancel", "cancellation",
              "yieldNow", "checkpoint", "run", "wait", "executor",
              "coroutine", "concurrency", "AsyncStream", "AsyncListener",
              "readable", "writable"]))


# ===========================================================================
# Cross compilation
# ===========================================================================
SECTIONS.append(Sec(
    "cross", "tooling", "Cross compilation",
    "Building for a machine that is not the one you are on. The compiler "
    "already emits code for any target LLVM knows; what a cross build needs "
    "beyond that is a toolchain to link with. For the common targets `rune` "
    "finds that toolchain itself; for the rest, the manifest names it.",
    [
        H("Foreign targets"),
        P("A handful of targets are built in, by name. Each knows its triple, "
          "where its toolchain is usually installed, and what can run its "
          "programs here, so building for one takes nothing but the name."),
        SH("""$ rune build --target wasm         # WebAssembly, with the WASI SDK
$ rune run --target windows        # built with mingw-w64, run under wine
$ rune test --target linux-arm64   # built with GCC, run under qemu"""),
        T(["Name", "Triple", "Builds with", "Runs here with"],
          [["`wasm`", "`wasm32-wasip1`", "the WASI SDK", "`wasmtime`, `wasmer` or `wasm3`"],
           ["`wasm-threads`", "`wasm32-wasip1-threads`", "the WASI SDK", "`wasmtime`, with threads on"],
           ["`windows`", "`x86_64-w64-mingw32`", "`x86_64-w64-mingw32-gcc`", "`wine`"],
           ["`linux-arm64`", "`aarch64-linux-gnu`", "`aarch64-linux-gnu-gcc`", "`qemu-aarch64`"],
           ["`linux-x64`", "`x86_64-linux-gnu`", "`x86_64-linux-gnu-gcc`", "`qemu-x86_64`"],
           ["`linux-riscv64`", "`riscv64-linux-gnu`", "`riscv64-linux-gnu-gcc`", "`qemu-riscv64`"]],
          caption="Each also answers to other spellings — `wasi`, `mingw`, "
                  "`linux-aarch64`, and its own triple."),
        P("`rune targets` lists them, and says for each whether its toolchain "
          "was found and whether this machine can run what it builds — and, "
          "when something is missing, how to get it. It works outside a "
          "package too."),
        SH("""$ rune targets
Foreign targets  (built in; --target <name>)
  wasm            wasm32-wasip1
      WebAssembly with WASI, built with the WASI SDK
      ✓ builds with /opt/wasi-sdk/bin/wasm32-wasip1-clang
      ✓ runs with wasmtime run -S inherit-env=y --dir=.
  windows         x86_64-w64-mingw32
      64-bit Windows, built with mingw-w64
      ✗ cannot find 'x86_64-w64-mingw32-gcc', which windows builds with
        install mingw-w64: `apt install gcc-mingw-w64-x86-64` or `brew install mingw-w64`
        or name another compiler: `cc = "..."` in [target.windows]
  ..."""),
        P("A name that is none of these is a mistake worth catching early, "
          "so a close one is suggested:"),
        SH("""$ rune build --target wams
● no target named 'wams'
  ─  note: did you mean 'wasm'?
  ─  note: `rune targets` lists every target this package can build for; a target triple works too"""),

        H("WebAssembly"),
        P("`--target wasm` builds a WebAssembly module that uses WASI for "
          "what a program needs from outside itself — standard streams, "
          "files, the clock, the environment, its arguments. The module is "
          "`<name>.wasm`, and anything that runs WASI runs it: wasmtime, "
          "wasmer, wasm3, a browser with a WASI shim, Node's `wasi` module."),
        P("It is built with the [WASI SDK](https://github.com/WebAssembly/wasi-sdk): "
          "clang, `wasm-ld` and wasi-libc in one directory. `rune` looks for "
          "it in `$WASI_SDK_PATH`, then where its installers put it — "
          "`/opt/wasi-sdk`, a versioned `/opt/wasi-sdk-*`, "
          "`~/.rune/toolchains/wasi-sdk`, `~/wasi-sdk` — and uses the "
          "`<triple>-clang` it ships, so nothing else has to be told the "
          "target."),
        SH("""$ export WASI_SDK_PATH=/opt/wasi-sdk-25.0-x86_64-linux
$ rune new hello && cd hello
$ rune run --target wasm
○ Preparing runtime for wasm32-wasip1
○ Compiling hello v0.1.0
○ Running target/wasm/debug/hello.wasm
Hello from hello!
$ wasmtime target/wasm/debug/hello.wasm"""),
        P("`rune run` and `rune test` start the module under the first of "
          "wasmtime, wasmer and wasm3 that is installed, with the current "
          "directory and the environment passed through — the two things a "
          "native program gets without asking, and a WASI one only when "
          "granted. A module run by hand gets only what its runner grants: "
          "`wasmtime --dir=. hello.wasm` to let it see the files here."),
        T(["", "`wasm`", "`wasm-threads`"],
          [["Files, streams, clock, environment, arguments", "yes", "yes"],
           ["`std::thread`", "no: starting one panics", "yes, with wasi-threads"],
           ["`std::task`", "a task that runs to the end without waiting; one "
            "that has to wait panics", "yes"],
           ["`std::net`", "no: every call fails", "no: every call fails"],
           ["`std::process` commands", "no: running one fails", "no"],
           ["Tracebacks", "the runner's own", "the runner's own"]],
          caption="What WASI preview 1 provides, and so what a module can do."),
        P("The differences are the platform's, not the compiler's. WASI "
          "preview 1 can use a socket it was handed but cannot make one, and "
          "has no processes. WebAssembly's stack is not memory a program can "
          "point at, so a task cannot be parked on one: under `wasm-threads` "
          "each task runs on a thread of its own, one at a time, and a switch "
          "is handing the processor from one to the next; under plain `wasm` "
          "there is only the one stack, and a task runs on it to the end."),
        N("A threaded module imports its memory rather than defining it — "
          "every thread is an instance of its own, and they share that "
          "memory — and fewer runtimes accept one. `wasm-threads` is a target "
          "of its own for that reason; wasmtime runs it with "
          "`-W threads=y -S threads=y`.",
          label="Why threads are a separate target"),
        P("Code that has to differ asks `@Config(family == \"wasm\")` or "
          "`@Config(os == \"wasi\")` — see **Conditional compilation** — and "
          "`std::arch` says `wasm32` with a 32-bit `usize`."),

        H("A target triple"),
        P("`--target` also takes a triple directly, for a target that needs "
          "no toolchain beyond the one already here — which is most of them "
          "when the host compiler can reach the target, as Apple's clang can "
          "reach `x86_64-apple-darwin`. The compiler emits a real object for "
          "that machine, in that machine's format."),
        SH("""$ runec --target x86_64-w64-mingw32 -c -o hello.o hello.rune
$ file hello.o
hello.o: Intel amd64 COFF object file"""),
        P("Linking is the part that needs help. A linker is platform "
          "software: it knows one set of startup files, one libc, one "
          "executable format. Cross-compiling means naming the one that "
          "belongs to the target — which is what a foreign target does for "
          "you, and what these flags do by hand."),
        T(["Flag", "Does"],
          [["`--target <triple>`", "what to emit for"],
           ["`--cc <program>`", "the toolchain driver that links"],
           ["`--sysroot <dir>`", "where that target's headers and libraries are"],
           ["`--runtime-dir <dir>`", "where its `libruneruntime.a` is"],
           ["`--link-arg <arg>`", "appended to the link command verbatim"],
           ["`--link-cxx`", "link the C++ runtime (implied by `extern \"C++\"`)"]]),
        SH("""$ runec --target wasm32-wasip1 \\
        --cc /opt/wasi-sdk/bin/wasm32-wasip1-clang \\
        --sysroot /opt/wasi-sdk/share/wasi-sysroot \\
        --runtime-dir ~/.rune/runtime/wasm32-wasip1 \\
        -o hello.wasm hello.rune"""),
        N("A driver named for its target — `x86_64-w64-mingw32-gcc`, "
          "`wasm32-wasip1-clang` — is already the right compiler and is left "
          "alone. Only a general one such as `clang` is told the target, "
          "because it is one binary for all of them.",
          label="Why `--cc` and `--target` are separate"),

        H("Naming a target in the manifest"),
        P("A package that is built for the same machines repeatedly, or "
          "whose toolchain is somewhere a foreign target would not look, "
          "says so once. `[target.<name>]` describes a toolchain; naming one "
          "does not build for it — `--target` does, or `[build] target` when "
          "the command line does not say."),
        P("A table may start from a foreign target and change only what "
          "differs: with `base`, or by being named after one and giving no "
          "`triple`. What it names wins; what it leaves out is found as the "
          "foreign target would find it."),
        S('''[package]
name = "report"
version = "0.1.0"

# The foreign target `wasm`, with the SDK somewhere of this project's own.
[target.wasm]
sdk = "../toolchains/wasi-sdk"

# Another name for it, run under a different runtime.
[target.web]
base = "wasm"
runner = "wasmer run --dir=."

# A target from scratch: everything named.
[target.pi]
triple = "aarch64-unknown-linux-gnu"
cc = "aarch64-linux-gnu-gcc"
sysroot = "/opt/pi-sysroot"
# How to run one of its binaries on *this* machine. Without it, `rune run`
# and `rune test` build and stop, rather than pretend.
runner = "qemu-aarch64 -L /opt/pi-sysroot"''', mode="frag",
          title="Three targets in Rune.toml"),
        T(["Key", "Means"],
          [["`base`", "the foreign target to start from"],
           ["`triple`", "passed to `runec --target`; needed unless there is a base"],
           ["`cc`", "the C driver that compiles the runtime and links"],
           ["`cxx`", "the C++ driver; derived from `cc` when absent"],
           ["`ar`", "the archiver; derived from `cc` when absent"],
           ["`sysroot`", "passed as `--sysroot`"],
           ["`sdk`", "where the WASI SDK is, for a target based on `wasm`"],
           ["`runner`", "how to start a built program here"],
           ["`runtime-dir`", "a prebuilt `libruneruntime.a` to use instead of building one"],
           ["`c-flags`", "added to every C compile for the target, the runtime's included"],
           ["`link`, `link-paths`, `link-args`", "native libraries the *target* needs, on top of the package's"]],
          caption="Relative paths are relative to Rune.toml."),
        SH("""$ rune targets
In Rune.toml
  wasm            wasm32-wasip1
      the foreign target wasm, adjusted
      ✓ builds with /work/toolchains/wasi-sdk/bin/wasm32-wasip1-clang
      ✓ runs with wasmtime run -S inherit-env=y --dir=.
  web             wasm32-wasip1
      the foreign target wasm, adjusted
      ...

$ rune build --target web
$ rune test --target pi             # built, then run under qemu"""),

        H("Where the output goes"),
        P("A cross build gets a directory of its own, named after the target, "
          "so host and cross artefacts never overwrite each other and "
          "switching between them rebuilds nothing. A foreign target asked "
          "for by another spelling — `--target wasi` — still builds into its "
          "own name's directory."),
        SH("""target/debug/report                # the host
target/windows/debug/report.exe    # --target windows
target/wasm/debug/report.wasm      # --target wasm
target/pi/debug/report             # --target pi"""),
        N("The `.exe` is added for a Windows target, because a PE image is "
          "only executable with it, and `.wasm` for WebAssembly, because "
          "every runtime expects it.", label="Executable suffix"),

        H("The runtime"),
        P("Every Rune program links a small runtime, half C and half Rune, "
          "and it is as target-specific as the program. `rune` builds it for "
          "a target the first time one is asked for and keeps it under "
          "`~/.rune/runtime/<triple>/`, using the same toolchain the rest of "
          "the build uses. A prebuilt one is used instead when "
          "`runtime-dir` names it."),
        SH("""$ rune build --target windows
○ Preparing runtime for x86_64-w64-mingw32
○ Compiling report v0.1.0
● Finished debug profile"""),

        H("Running what you built"),
        P("A binary for another machine cannot simply be started. With a "
          "runner — the foreign target's, when one is installed, or the "
          "table's `runner` — `rune run` and `rune test` go through it; "
          "without one they build and say so, rather than reporting a test "
          "as passed when it never ran."),
        SH("""$ rune run --target windows        # wine is not installed
● built for x86_64-w64-mingw32, which this machine cannot run
  ─  note: install wine to run it here, or copy it to a machine that can"""),

        H("C and C++ sources"),
        P("A package with a C or C++ half lists it, and `rune` compiles it "
          "with whichever toolchain the build is using — so the native code "
          "crosses along with the Rune, and a package with an FFI shim needs "
          "nothing special to target another machine, WebAssembly included. "
          "The C++ driver is the one that goes with `cc` unless "
          "`[target.<name>] cxx` names another."),
        S('''[build]
c-sources = ["c/shim.c"]
c-flags = ["-Wall", "-Wextra"]
cxx-sources = ["cxx/shim.cpp"]
cxx-flags = ["-Wall", "-Wextra"]''', mode="frag",
          title="Compiled with the build's own cc and c++"),

        H("What does not cross"),
        P("The generated code is correct for every target LLVM supports. A "
          "few things are narrower than that:"),
        T(["", "State"],
          [["Struct by value across the C boundary",
            "not on Windows x64 — rejected rather than misread; see "
            "**Calling C**"],
           ["A panic message on Windows",
            "the program aborts as it should, but the text does not reach "
            "standard error"],
           ["`extern \"C++\"`",
            "every Itanium-ABI target — Linux, macOS, the BSDs, MinGW, "
            "WebAssembly; not `-windows-msvc`"],
           ["Threads, sockets and processes on WebAssembly",
            "what WASI preview 1 provides; see **WebAssembly** above"],
           ["Shared libraries on WebAssembly",
            "none — a library is a `.rul`, linked into the module"],
           ["Everything else in the FFI",
            "portable: scalars, pointers, `CString`, `@cfunction`, `@export`"]]),

        H("32-bit targets"),
        P("`usize` and `isize` are the target's pointer width, so a 32-bit "
          "build — `wasm32`, `i686` — sizes them at 4 bytes and everything "
          "measured in them — an allocation, `mem::offset`, a container's "
          "index — follows. The standard library declares C's `size_t` as "
          "`usize` and a file offset as `isize` for the same reason: a `u64` "
          "in either place would pass a doubled argument to libc on such a "
          "target."),
        SH("""$ runec --target i686-w64-mingw32 -c -o hello.o hello.rune
$ rune test --target wasm           # wasm32: a 32-bit target too"""),
        N("C's `long` is pointer-sized everywhere Rune targets except 64-bit "
          "Windows, where it stays 32 bits. `isize` is the closest spelling "
          "Rune has, and it is what the file-offset declarations use; on "
          "Windows x64 the value rides in the low half of the register, which "
          "is why the wider spelling works there.",
          label="Where `isize` is not exactly `long`"),
        N("Nothing here changes the language. A program that cross-compiles "
          "is the same program; only the toolchain around it differs.",
          label="No conditional compilation"),
    ],
    keywords=["cross", "target", "triple", "mingw", "windows", "toolchain",
              "sysroot", "runner", "wine", "qemu", "c-sources", "runtime",
              "cc", "linker", "exe", "arm", "aarch64", "32-bit", "i686",
              "wasm32", "usize", "isize", "pointer width", "size_t",
              "foreign target", "wasm", "webassembly", "wasi", "wasi-sdk",
              "wasmtime", "wasmer", "wasm-threads", "base", "sdk",
              "rune targets"]))


# ===========================================================================
# Bare metal
# ===========================================================================
SECTIONS.append(Sec(
    "baremetal", "tooling", "Bare metal",
    "Programs with nothing underneath them: a kernel, a boot loader, "
    "firmware. The language is the same one — the checks, the borrow "
    "checker, classes and optionals included — and what the generated code "
    "needs of a runtime is Rune compiled into the program, asking the "
    "program for the three things only it can know.",
    [
        H("A freestanding program"),
        P("Two directives at the top of a file — or `runtime` and `entry` "
          "under `[build]`, or `--runtime none` and `--entry none` to "
          "`runec` — say what the program has around it."),
        T(["Directive", "Means", "Like Rust's"],
          [["`@runtime(none)`", "no C library and no hosted runtime are "
            "linked; `runetime/freestanding.rune` is compiled into the "
            "program instead", "`#![no_std]`"],
           ["`@entry(none)`", "no `main` is generated; an `@export`ed "
            "function is where execution starts", "`#![no_main]`"]]),
        S("""@runtime(none)
@entry(none)
import std::asm

@panicHandler
fn panicked(message: CString, location: CString) -> Never {
    // say it somewhere (a serial port, the screen), then stop
    loop { unsafe { asm::run("cli; hlt", "") } }
}

@export("kernel_main")
fn kernelMain(magic: u32, info: u32) -> Never {
    // ...
    loop {}
}""", mode="frag", title="The shape of a kernel"),
        P("The standard library is still there, and still only compiled "
          "where it is used, so `T?`, `Result`, `std::asm` and the rest of "
          "what is plain Rune work as ever. What reaches the hosted runtime "
          "— `String`, `std::io`, threads, tasks, reference counting — is "
          "refused at compile time, against the function of yours that "
          "reached it:"),
        SH("""● kernel.rune [4:3..8]
4 ║ fn greet() { io::println("hi") }
       ^^^^^ ERROR: 'greet' needs the hosted runtime, and this program is built without one [E0542]
    ─  note: it uses `String`, which lives in the hosted runtime; a freestanding program works in `CString` and byte arrays"""),
        N("A freestanding program is built with `--memory zombie`. Every "
          "object has one owner and nothing is counted, so the heap needs "
          "nothing but an allocator; reference counting would need the "
          "hosted runtime's atomics and weak table, and is refused like any "
          "other use of it.", label="Single ownership"),

        H("The hooks"),
        P("Three functions the generated code relies on, supplied by the "
          "program. Each has a default in the freestanding runtime, marked "
          "`@weak`, that the program's own replaces."),
        T(["Attribute", "Signature", "Called for", "Default"],
          [["`@panicHandler`", "`fn(message: CString, location: CString) -> Never`",
            "every failed check, `unwrap` of an empty `Option`, "
            "`process::panic`", "stops where it is, for ever"],
           ["`@allocator`", "`fn(size: usize, align: usize) -> *var u8`",
            "every object a class, a closure or a `Unique` makes",
            "panics: *this program allocates, and declares no @allocator*"],
           ["`@deallocator`", "`fn(block: *var u8)`",
            "every object whose one owner is done with it",
            "nothing — without an allocator nothing was allocated"]],
          caption="A hook with the wrong signature is E0248; two of one kind "
                  "is E0249."),
        S("""global var region: [65536:u8] = [0; 65536]
global var used: usize = 0

@allocator
fn allocate(size: usize, align: usize) -> *var u8 {
    let at = (used + align - 1) / align * align
    used = at + size
    unsafe { &var region[at as i64] as *var u8 }
}

@deallocator
fn release(block: *var u8) {}""", mode="frag",
          title="The smallest allocator: a bump pointer that never frees"),

        H("Safety on bare metal"),
        P("Nothing about checking changes. `--safety full` inserts every "
          "check it inserts anywhere — array bounds, integer overflow, "
          "division by zero, a nil dereference, a `match` no arm matched, "
          "a drop of an object that turns out to be shared — and the Zombie "
          "borrow checker proves the same things at compile time. The only "
          "difference is where a failed check goes: the freestanding "
          "runtime formats it into a buffer of its own, not the heap, and "
          "hands it to the `@panicHandler`."),
        SH("""KERNEL PANIC: index 4 is out of bounds for a collection of length 4
  at main.rune:53:34"""),

        H("What the freestanding runtime provides"),
        P("`runetime/freestanding.rune` is compiled into the program for "
          "the program's own target, the way Rust builds `core` for the "
          "target it compiles for. It depends on nothing."),
        T(["", "Provides"],
          [["Panics", "`rune_panic_bounds`, `_overflow`, `_div_zero`, `_nil`, "
            "`_no_match`, `_any`, `_unwrap` and `rune_panic`, each turned "
            "into a message for the `@panicHandler`"],
           ["The heap", "`rune_alloc` and `rune_drop` over the `@allocator` "
            "and `@deallocator`: an object's header, its `deinit`, and "
            "`--safety full`'s check that a dropped object had one owner"],
           ["Memory", "`memcpy`, `memmove`, `memset`, `memcmp` — LLVM emits "
            "calls to them for large copies whatever the target"],
           ["32-bit targets", "`__divdi3`, `__udivdi3`, `__moddi3` and "
            "`__umoddi3`: 64-bit division, which a 32-bit processor does "
            "with a library call"],
           ["Classes", "`$clone()`, `is` and `Any` checks, hashing"]]),
        N("A freestanding program is compiled with `no-builtins`, as C's "
          "`-ffreestanding` does, so LLVM never turns a loop that copies "
          "bytes into a call to `memcpy` — least of all inside `memcpy`. "
          "Every one of these is `@weak`: a program with faster ones keeps "
          "its own.", label="Why the loops stay loops"),

        H("Starting without main"),
        P("Under `@entry(none)` nothing runs before the program's own entry "
          "— which is usually a few lines of assembly that make a stack and "
          "call it. A global whose value is a constant or all zeros is in "
          "the image already. Anything else is set by `rune_init`, which the "
          "compiler generates and the entry calls first:"),
        S("""extern "C" { fn rune_init() }

@export("kernel_main")
fn kernelMain(magic: u32, info: u32) -> Never {
    unsafe { rune_init() }
    // ...
}""", mode="frag"),

        H("@weak"),
        P("A definition another may replace: the linker keeps a strong "
          "definition of the same symbol over it, and so does the compiler "
          "when both are in one program. It is how the freestanding "
          "runtime's defaults give way to a program's hooks, and it works "
          "the same for any `@export`ed function of your own."),

        H("Bare-metal targets"),
        P("Four foreign targets build with this machine's clang and ld.lld "
          "and are freestanding whatever the sources say."),
        T(["Name", "Triple", "Runs here with"],
          [["`bare-x86`", "`i686-unknown-none-elf`", "`qemu-system-i386 -kernel`: a multiboot kernel"],
           ["`bare-x86_64`", "`x86_64-unknown-none-elf`", "—"],
           ["`bare-arm64`", "`aarch64-unknown-none-elf`", "`qemu-system-aarch64 -M virt -kernel`"],
           ["`bare-riscv64`", "`riscv64-unknown-none-elf`", "`qemu-system-riscv64 -M virt -bios none -kernel`"]]),
        P("A kernel says where its sections go with a linker script, and "
          "brings what has to run before any Rune can — the multiboot "
          "header, a stack — as an assembly file among its `c-sources`, "
          "which clang assembles like any other."),
        S("""[build]
safety = "full"
runtime = "none"
entry = "none"
target = "bare-x86"
c-sources = ["boot/boot.s"]
linker-script = "kernel.ld"

[target.bare-x86]
runner = "qemu-system-i386 -display none -serial stdio -device isa-debug-exit,iobase=0xf4,iosize=0x04 -kernel\"""",
          mode="frag", title="examples/toyos/Rune.toml"),
        SH("""$ cd examples/toyos
$ rune run                          # boots under QEMU, runs its checks, powers off
$ rune run -- -append panic         # trips a bounds check on purpose; exits 3"""),
        P("[`examples/toyos`](examples/toyos/README.md) is the whole of it "
          "worked through: a multiboot kernel with a VGA console and a "
          "serial one, a first-fit heap behind its `@allocator`, processes "
          "on a round-robin scheduler, each owned by the one before it, and "
          "64-bit arithmetic on a 32-bit processor — all of it under "
          "`--safety full`."),
    ],
    keywords=["bare metal", "freestanding", "kernel", "os", "no_std",
              "no_main", "runtime", "entry", "@runtime", "@entry",
              "panicHandler", "allocator", "deallocator", "weak", "rune_init",
              "multiboot", "qemu", "linker script", "linker-script",
              "bare-x86", "bare-arm64", "bare-riscv64", "E0542", "E0248",
              "E0249", "memcpy", "__udivdi3", "firmware", "embedded"]))


# ===========================================================================
# Conditional compilation
# ===========================================================================
SECTIONS.append(Sec(
    "config", "tooling", "Conditional compilation",
    "`@Config(...)` decides whether a declaration exists at all. It is "
    "answered before anything is checked, so what it rules out is not merely "
    "unused \u2014 it is gone, and may name types and foreign symbols that "
    "exist on no other target.",
    keywords=["cfg", "config", "conditional", "platform", "target", "feature",
              "os", "arch", "windows", "linux", "macos", "portability",
              "[config]", "backend", "value", "rune add --config"],
    items=[
        H("`@Config`"),
        P("Write the condition on the declaration. Two definitions of one name "
          "under conditions that cannot both hold are the ordinary way to give "
          "a function a different body per platform."),
        S("""import std::io

@Config(family == "unix")
fn lineEnding() -> String { "\\n" }

@Config(family == "windows")
fn lineEnding() -> String { "\\r\\n" }

fn main() -> i64 {
    io::println("bytes in a line ending: " + lineEnding().$length().$str())
    0
}""", mode="run", title="One name, one definition per family"),
        N("A ruled-out declaration is removed after parsing and before "
          "anything is collected, so its body is never name-resolved and never "
          "type-checked. It still has to *parse* \u2014 it is Rune, not text "
          "\u2014 but it may call functions that exist on no other platform.",
          label="Gone, not unused"),

        H("What a condition can ask"),
        T(["Key", "Is"],
          [["`os`", "`macos`, `windows`, `linux`, `ios`, `android`, `freebsd`, "
            "`openbsd`, `netbsd`, `solaris`, `wasi`"],
           ["`arch`", "`aarch64`, `x86_64`, `x86`, `arm`, `riscv32`, "
            "`riscv64`, `wasm32`, `wasm64`, `powerpc64`"],
           ["`family`", "`unix`, `windows` or `wasm`"],
           ["`pointer_width`", "`\"32\"` or `\"64\"`"],
           ["`endian`", "`little` or `big`"],
           ["`target`", "the full triple being built for"],
           ["`safety`", "`none`, `minimal` or `full`"],
           ["`memory`", "`arc` or `zombie`"],
           ["`opt_level`", "`\"0\"` through `\"3\"`"]]),
        P("Those are compared against a string. Everything else is a name that "
          "is either set or not, written on its own:"),
        T(["Name", "Set when"],
          [["`debug`", "the build carries debug information (`-g`)"],
           ["any `--cfg <name>`", "the command line said so"],
           ["any `[build] cfg` entry", "the manifest said so"],
           ["a dependency's name", "that dependency is in `[dependencies]`"]]),
        P("A condition joins those with `&&`, `||`, `!` and parentheses. There "
          "is nothing else in the language: no arithmetic, no calls, no "
          "variables. It has to be answerable before the type checker has "
          "run."),
        S("""@Config(arch == "aarch64" && os == "macos")
fn tuned() -> i64 { 1 }

@Config(!(arch == "aarch64" && os == "macos"))
fn tuned() -> i64 { 0 }

@Config(debug)
fn checking() -> bool { true }

@Config(!debug)
fn checking() -> bool { false }""", title="Conditions compose"),

        H("Conditions on members"),
        P("A field, a method, an enum variant or a foreign declaration may "
          "carry one of its own. Fields that survive are renumbered, so a "
          "struct is laid out as though the others were never written, and a "
          "`match` is exhaustive over the variants that exist."),
        S("""import std::io

struct Handle {
    pub id: i64,
    @Config(os == "windows")
    pub winHandle: i64,
    @Config(family == "unix")
    pub fd: i64,
}

extend Handle {
    @Config(family == "unix")
    pub fn describe(&self) -> String { "fd " + self.fd.$str() }

    @Config(os == "windows")
    pub fn describe(&self) -> String { "handle " + self.winHandle.$str() }
}

fn main() -> i64 {
    io::println(Handle { id: 1, fd: 3 }.describe())
    0
}""", mode="run", title="A type with a different shape per platform"),

        H("Keys with a value of your own"),
        P("A name that is either set or not answers a yes-or-no question. A "
          "key with a value answers a *which* question, which is what a "
          "package wants when it has three backends rather than one optional "
          "one. Declare the keys a package understands, and their defaults, "
          "in a `[config]` table; every one is then comparable in a "
          "condition."),
        S("""[package]
name = "gfx"
version = "0.1.0"

[config]
backend = "software"
api_level = 1
tracing = false""", mode="frag", title="Rune.toml"),
        S("""@Config(backend == "metal")
fn present() -> String { "metal" }

@Config(backend == "vulkan")
fn present() -> String { "vulkan" }

@Config(backend != "metal" && backend != "vulkan")
fn present() -> String { "software" }

// `=` reads as the comparison: there is nothing else it could mean in a
// condition, and `tracing = true` is how the key was written in the manifest.
@Config(tracing = true)
fn trace(what: String) { /* ... */ }

@Config(tracing = false)
fn trace(what: String) {}

@Config(api_level == 3)
fn modern() -> bool { true }""", mode="frag", title="What a value key answers"),
        P("The value may be a string, a number, a boolean or a bare word, and "
          "either side of the comparison may be the key — "
          "`@Config(\"metal\" == backend)` says the same thing. A key the "
          "package never declared is an error rather than a silently false "
          "condition, so a typo is caught where it is written."),
        SH("""$ runec --cfg backend=metal --cfg api_level=3 --cfg tracing=true app.rune
$ rune build --cfg backend=vulkan"""),

        H("Choosing a dependency's configuration"),
        P("A package that depends on `gfx` says which backend it wants where "
          "it names the dependency, so one build of an application does not "
          "have to be one build of everything under it."),
        S("""[dependencies]
gfx = { path = "../gfx", config = { backend = "metal", tracing = true } }""",
          mode="frag", title="Rune.toml"),
        SH("""$ rune add gfx --config backend=vulkan"""),
        P("The values a dependency was built with travel with it. A compiled "
          "`.rul` records them, and its interface is re-read with the answers "
          "*it* was built under — so a library's own `@Config` can never be "
          "re-decided by whoever imports it."),
        T(["Situation", "What happens"],
          [["a key the package never declared", "an error naming the package "
            "and listing the keys it has"],
           ["two dependents choosing differently for one package",
            "an error naming both, since one build cannot be two things"],
           ["nobody choosing", "the default from `[config]`"],
           ["a builtin key (`os`, `memory`, ...)",
            "refused: the target already answers those"]]),
        N("A value key and a bare name are the same mechanism. `--cfg "
          "tracing` sets `tracing` with no value, which `@Config(tracing)` "
          "answers true and `@Config(tracing = false)` answers false.",
          label="One mechanism, two spellings"),

        H("Features and dependencies"),
        P("`--cfg` on the command line and `cfg` in the manifest set names of "
          "your own. Every dependency's name is set too, so a package can ask "
          "whether it has one without anybody writing it down twice."),
        S("""[package]
name = "report"
version = "0.1.0"

[build]
cfg = ["telemetry"]

[dependencies]
statistics = { path = "../statistics" }""",
          mode="frag", title="Rune.toml"),
        S("""@Config(telemetry)
fn record(event: String) { /* ... */ }

@Config(!telemetry)
fn record(event: String) {}

// True exactly when `statistics` is in [dependencies].
@Config(statistics)
fn summarise() -> String { "with statistics" }""",
          title="What that makes true"),
        SH("""$ rune build                     # telemetry, statistics
$ rune build --cfg verbose       # telemetry, statistics, verbose"""),
        N("`@Config` applies to declarations, not to statements. To make part "
          "of a body conditional, put it in a function of its own and give "
          "every branch a definition \u2014 which also means a missing case is "
          "a name that cannot be found, rather than silence.",
          label="Declarations only"),
    ]))


# ===========================================================================
# Inline assembly
# ===========================================================================
SECTIONS.append(Sec(
    "asm", "interop", "Inline assembly",
    "`std::asm` hands instructions to the assembler as written. It is the "
    "least portable thing in the language and the compiler checks none of it.",
    keywords=["asm", "assembly", "inline", "intrinsic", "constraint",
              "register", "barrier", "unsafe"],
    items=[
        H("Two calls"),
        T(["Call", "Does"],
          [["`asm::run(text, constraints, ...)`",
            "Runs the instructions for their effects. Never dropped, never "
            "merged with an identical call."],
           ["`asm::value<R>(text, constraints, ...) -> R`",
            "Runs them and takes the result as `R`. Treated as a pure "
            "function of its inputs."]]),
        P("Both are `@unsafe`, so a call needs `unsafe { ... }` or a caller "
          "that is itself `@unsafe`. The instructions and the constraints have "
          "to be written out at the call: they are assembled with the program, "
          "so they cannot be computed while it runs."),
        S("""import std::io
import std::asm

@Config(arch == "aarch64")
fn addUp(a: i64, b: i64) -> i64 {
    unsafe { asm::value<i64>("add $0, $1, $2", "=r,r,r", a, b) }
}

@Config(arch == "x86_64")
fn addUp(a: i64, b: i64) -> i64 {
    unsafe { asm::value<i64>("addq $2, $0", "=r,0,r", a, b) }
}

@Config(arch != "aarch64" && arch != "x86_64")
fn addUp(a: i64, b: i64) -> i64 { a + b }

fn main() -> i64 {
    io::println(addUp(2, 40).$str())
    0
}""", mode="run", title="One instruction, per architecture"),

        H("Operands and constraints"),
        P("`$0` is the result where there is one and the first input where "
          "there is not; the inputs follow in the order they were written. The "
          "constraint string lists one constraint per operand, comma "
          "separated, results first \u2014 it is the notation LLVM and GCC "
          "share."),
        T(["Constraint", "Means"],
          [["`r`", "an input in a general register"],
           ["`=r`", "a result written to a general register"],
           ["`0`", "this input must land where operand 0 did"],
           ["`i`", "an immediate the assembler can fold in"],
           ["`m`", "a memory operand"]]),
        P("LLVM is asked whether the constraints fit the call before anything "
          "is emitted, so a mismatch is a diagnostic pointing at the call "
          "rather than a failure inside the back end."),
        S("""import std::asm

fn broken() -> i64 {
    // Three inputs promised, none supplied.
    unsafe { asm::value<i64>("nop", "=r,r,r,r") }
}""", mode="diag", title="Constraints that do not fit"),

        H("Which one to reach for"),
        P("`asm::value` is a function of its inputs, so the compiler may drop "
          "the call when nothing uses the result and keep one copy where the "
          "same call appears twice on the same values. That is right for "
          "arithmetic and wrong for anything that answers differently each "
          "time \u2014 a clock, a counter, a device register. Use `asm::run` "
          "for those, and for barriers, fences and syscalls."),
        S("""import std::asm

@Config(arch == "aarch64")
fn barrier() { unsafe { asm::run("dmb ish", "") } }

@Config(arch == "x86_64")
fn barrier() { unsafe { asm::run("mfence", "") } }""",
          title="Written for its effects"),
        N("Assembly is text for one machine. Anything using it wants a "
          "`@Config(arch == \"...\")` around it and a definition for the "
          "architectures you do not handle \u2014 otherwise the build fails on "
          "the first machine nobody thought about.",
          label="Always paired with `@Config`", tone="warn"),
    ]))


# ===========================================================================
# Grammar and limitations
# ===========================================================================
SECTIONS.append(Sec(
    "grammar", "appendix", "Grammar and limits",
    "A condensed grammar for the whole language, the operator precedence "
    "table, every keyword, and an honest list of what is not implemented yet.",
    [
        H("Declarations"),
        G("""module        ::= { import | declaration }
import        ::= "import" path [ "as" identifier ]
path          ::= identifier { "::" identifier }

declaration   ::= { decorator } [ "pub" ] item
item          ::= function | struct | class | enum | mark | bind
                | extend | global | typealias | externBlock

function      ::= [ "async" ] "fn" identifier [ generics ] "(" params ")"
                  [ "->" type ] { where } block
params        ::= [ param { "," param } [ "," "..." ] ]
param         ::= [ label ] identifier ":" type [ "=" expression ]
                | [ "&" [ "var" ] ] "self"

struct        ::= "struct" identifier [ generics ] "{" { field } "}"
class         ::= "class" identifier [ generics ] [ ":" type ]
                  [ "+" mark { "+" mark } ] "{" { member } "}"
enum          ::= "enum" identifier [ generics ] "{" { variant } "}"
variant       ::= identifier [ "(" type { "," type } ")"
                            | "{" { field } "}" ] [ "=" expression ]
field         ::= [ "pub" ] [ "weak" ] [ "var" ] identifier ":" type
mark          ::= "mark" identifier [ generics ] [ ":" mark { "+" mark } ]
                  "{" { requirement } "}"
bind          ::= "bind" [ generics ] bindHead [ where ]
                  "{" { member } "}"
bindHead      ::= bindTarget "to" type
                | type "into" type
bindTarget    ::= markPath [ "<" type { "," type } ">" ]
                | "operator" "::" ( identifier | stringLit )
extend        ::= "extend" type [ where ] "{" { member } "}"
global        ::= "global" identifier ":" type "=" expression
typealias     ::= "type" identifier [ generics ] "=" type
externBlock   ::= "extern" stringLit "{" { externItem } "}"
externItem    ::= externFn | externVar | cxxNamespace | cxxType
cxxNamespace  ::= "namespace" identifier "{" { externItem } "}"
cxxType       ::= ( "class" | "struct" ) identifier [ generics ]
                  [ ":" identifier ] "{" { member } "}"
                | enum

generics      ::= "<" genericParam { "," genericParam } ">"
genericParam  ::= identifier [ ":" bound { "+" bound } ]
where         ::= "where" type ":" bound { "," type ":" bound }"""),

        H("Statements"),
        G("""block         ::= "{" { statement } [ expression ] "}"
statement     ::= binding | assignment | expression | control | defer
                | declaration

binding       ::= ( "let" | "var" | "mut" ) pattern [ ":" type ]
                  "=" expression
                | identifier [ ":" type ] "=" expression
assignment    ::= place assignOp expression
assignOp      ::= "=" | "+=" | "-=" | "*=" | "/=" | "%="
                | "&=" | "|=" | "^=" | "<<=" | ">>="

control       ::= "return" [ expression ]
                | "break" [ label ] [ expression ]
                | "continue" [ label ]
defer         ::= "defer" block

ifExpr        ::= "if" expression block
                  { "elif" expression block } [ "else" block ]
whileExpr     ::= "while" expression block
loopExpr      ::= "loop" block
forExpr       ::= "for" pattern "in" expression block
member        ::= expression "." [ "$" ] ( identifier | integer )
await         ::= expression "." "await"
closure       ::= [ "move" ] "||" [ "(" params ")" ] [ "->" type ] block
asyncExpr     ::= "async" ( block | closure )
markCall      ::= identifier "::" markPath "." identifier "(" [ args ] ")"
matchExpr     ::= "match" expression "{" { matchArm } "}"
matchArm      ::= pattern { "|" pattern } [ "if" expression ]
                  "=>" ( expression | block )
unsafeBlock   ::= "unsafe" block"""),
        P("A newline ends a statement when what precedes it can end one and "
          "what follows can begin one — inside brackets, after an operator, or "
          "before a leading `.`, it does not. Semicolons are always allowed and "
          "never required."),

        H("Patterns"),
        G("""pattern       ::= "_"
                | literal
                | [ "let" | "var" ] identifier
                | path [ "(" pattern { "," pattern } ")" ]
                | path "{" identifier [ ":" pattern ] { "," … } [ ".." ] "}"
                | "(" pattern { "," pattern } ")"
                | "[" pattern { "," pattern } "]"
                | expression ( ".." | "..=" ) expression
                | pattern "|" pattern"""),

        H("Types"),
        G("""type          ::= path [ "<" type { "," type } ">" ]
                | "&" [ "var" ] type              // borrow
                | "*" [ "var" ] type              // raw pointer
                | "[" expression ":" type "]"     // array
                | "[" type "]"                    // slice
                | "(" type { "," type } ")"       // tuple
                | type "?"                        // Option<type>
                | "@function" "(" [ type { "," type } ] ")" [ "->" type ]
                | "dyn" markPath
                | "some" markPath
                | "Self"
"""),

        H("Operator precedence"),
        T(["", "Operators", "Associativity"],
          [["1", "`.` `::` `()` `[]` `?` `.await`", "left"],
           ["2", "`-` `!` `~` `&` `&var` `*` (prefix)", "right"],
           ["3", "`as` `into`", "left"],
           ["4", "`*` `/` `%`", "left"],
           ["5", "`+` `-`", "left"],
           ["6", "`<<` `>>`", "left"],
           ["7", "`&`", "left"],
           ["8", "`^`", "left"],
           ["9", "`\\|`", "left"],
           ["10", "`..` `..=`", "none"],
           ["11", "`<` `<=` `>` `>=`", "left"],
           ["12", "`==` `!=`", "left"],
           ["13", "`is`", "left"],
           ["14", "`&&`", "left"],
           ["15", "`\\|\\|`", "left"],
           ["16", "`??`", "right"],
           ["17", "`=` and every compound assignment", "right"]],
          caption="Tightest first. `..` does not associate: write parentheses "
                  "if you meant to nest ranges."),

        H("Keywords"),
        T(["Group", "Words"],
          [["declaration", "`fn` `struct` `class` `enum` `mark` `bind` `to` "
            "`extend` `type` `global` `extern` `import` `pub` `where`"],
           ["binding", "`let` `var` `mut` `weak`"],
           ["control", "`if` `elif` `else` `while` `loop` `for` `in` `match` "
            "`return` `break` `continue` `defer`"],
           ["value", "`self` `Self` `super` `true` `false` `nil`"],
           ["operator-like", "`as` `into` `is` `operator` `dyn` `unsafe`"],
           ["sigil", "`$` before a member name: the compiler's own"]]),

        H("Not implemented yet"),
        P("Stated plainly, because a reference that hides its edges wastes your "
          "time."),
        T(["Area", "Where it stops"],
          [["packages", "`{ path = ... }` only — no registry, no version "
            "resolution"],
           ["collections", "arrays, slices, `Vector`, `Map` and `Set`; no "
            "ordered map, and no persistent collections"],
           ["concurrency", "threads, channels, atomics, `Send`, `Sync`, "
            "`Arc` and `Mutex` on pthreads platforms; tasks with `async fn` "
            "and `.await` on one thread; no `select`, no cancellation, a "
            "thread entry is a `fn` rather than a closure, and the Windows "
            "thread backing still faults when a handle is destroyed and "
            "another thread spawned"],
           ["generics", "monomorphised; no higher-kinded parameters, and no "
            "specialisation — a more specific implementation cannot displace "
            "a general one"],
           ["marks", "associated types, their bounds and `where` clauses are "
            "all checked; a mark still cannot require an operator on an "
            "associated type of another mark"],
           ["leaks", "at `--safety full` a class that can reach itself "
            "strongly is refused, so reference cycles cannot be built; the "
            "rule reads types rather than objects, so it also refuses shapes "
            "that would not have looped, and it cannot see a closure's "
            "captures"],
           ["closures", "captures are copied into a heap environment when the "
            "closure is made; `move` says so explicitly, and a class is how "
            "you share one value instead"],
           ["strings", "UTF-8 with character indexing; `std::text` does NFC/"
            "NFD and a root collation, but not the Unicode Collation "
            "Algorithm and not per-locale ordering"],
           ["errors", "`Result` and `?` propagate; a panic aborts and cannot "
            "be caught — there is no unwinder, and unwinding past a scope "
            "would have to release everything it owns"],
           ["debug info", "`-g` emits subprograms, line tables, types and "
            "local variables, and a traceback on abort; lexical sub-scopes "
            "are flattened into the function, so a shadowed name shows the "
            "outer one"]]),
        N("None of these are load-bearing for the language's design — they are "
          "unwritten, not blocked.", label="Direction"),
    ],
    keywords=["grammar", "ebnf", "precedence", "keywords", "limitations",
              "appendix", "syntax"]))
