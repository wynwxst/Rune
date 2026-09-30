// Tests the TextMate grammar with the engine VS Code itself uses.
//
//     npm test            (from editors/vscode, after `npm install`)
//
// Two kinds of check: named tokens in small samples get the scopes they
// should, and every `.rune` file in the repository tokenises with no line
// left inside a string or comment that never ends — the usual way a grammar
// goes wrong on real code.

"use strict";

const fs = require("fs");
const path = require("path");
const vsctm = require("vscode-textmate");
const oniguruma = require("vscode-oniguruma");

const here = __dirname;
const root = path.resolve(here, "..", "..", "..");

async function loadGrammar() {
  const wasm = fs.readFileSync(require.resolve("vscode-oniguruma/release/onig.wasm")).buffer;
  await oniguruma.loadWASM(wasm);
  const registry = new vsctm.Registry({
    onigLib: Promise.resolve({
      createOnigScanner: (sources) => new oniguruma.OnigScanner(sources),
      createOnigString: (s) => new oniguruma.OnigString(s),
    }),
    loadGrammar: async (scope) => {
      if (scope !== "source.rune") return null;
      const file = path.join(here, "..", "syntaxes", "rune.tmLanguage.json");
      return vsctm.parseRawGrammar(fs.readFileSync(file, "utf8"), file);
    },
  });
  return registry.loadGrammar("source.rune");
}

/** Every token of `text`, as { text, scopes, line }. */
function tokenize(grammar, text) {
  const out = [];
  let state = vsctm.INITIAL;
  text.split("\n").forEach((line, n) => {
    const r = grammar.tokenizeLine(line, state);
    for (const t of r.tokens) {
      out.push({ text: line.substring(t.startIndex, t.endIndex), scopes: t.scopes, line: n });
    }
    state = r.ruleStack;
  });
  return { tokens: out, state };
}

let failures = 0;
function check(name, ok, detail) {
  console.log((ok ? "ok   " : "FAIL ") + name);
  if (!ok) {
    failures++;
    if (detail !== undefined) console.log("     " + JSON.stringify(detail).slice(0, 600));
  }
}

/** The innermost scopes of the first token whose text is exactly `text`, after `after` tokens of the same text. */
function scopesOf(tokens, text, nth = 0) {
  const found = tokens.filter((t) => t.text === text);
  return found[nth] ? found[nth].scopes : [];
}

function has(tokens, text, scope, nth = 0) {
  return scopesOf(tokens, text, nth).some((s) => s === scope || s.startsWith(scope + "."));
}

const SAMPLE = `import std::io
import std::collections::{vector, slice}

/// A point, with \`x\` and \`y\`.
pub struct Point {
    pub x: f64
    pub y: f64
}

@safe("the runtime owns it")
pub fn describe(p: &Point, count: i64) -> String {
    let label = "at {} and {:>8.2}\\n"
    var total = 0x1F_u8 + 0b1010 + 1_000i64 + 2.5e-3
    for i in 0..count { total += i }
    if p.x == 1.0 && !done { return nil }
    let r = r#"raw "quoted" text"#
    let c = '\\n'
    let s = p.name.$length()
    let opt: Option<Self>? = Some(self)
    io::println(format!("{}", s))
    MAX_DEPTH ?? -1
    /* block /* nested */ still */
    let block = """
        one
        two
        """
    let f: @function(i64) -> bool = ||(n: i64) -> bool { n > 0 }
    @lint(allow(unused-variable), warn(todo))
    match opt { Some(v) => v, None => 0 }
}

bind io::Display to Point {
    fn display(&self) -> String { self.x.$str() }
}

type Alias = vector::Vector<Point>
global var failed: bool = false
macro twice { ($e: expr) => { $e + $e } }
`;

(async () => {
  const grammar = await loadGrammar();
  const { tokens } = tokenize(grammar, SAMPLE);

  check("`import` is a keyword", has(tokens, "import", "keyword.other.import.rune"));
  check("import paths are namespaces", has(tokens, "collections", "entity.name.namespace.rune"));
  check("a grouped import's members are namespaces", has(tokens, "slice", "entity.name.namespace.rune"));
  check("`///` is a doc comment", has(tokens, "///", "punctuation.definition.comment.rune") &&
        scopesOf(tokens, "///").includes("comment.line.documentation.rune"));
  check("code in a doc comment is marked", has(tokens, "`x`", "markup.inline.raw.rune"));
  check("a struct's name is a type", has(tokens, "Point", "entity.name.type.rune"));
  check("`pub` is a modifier", has(tokens, "pub", "storage.modifier.rune"));
  check("a decorator is marked", has(tokens, "safe", "entity.name.function.decorator.rune"));
  check("a function's name", has(tokens, "describe", "entity.name.function.rune"));
  check("primitive types", has(tokens, "i64", "support.type.primitive.rune") && has(tokens, "f64", "support.type.primitive.rune"));
  check("String is a support type", has(tokens, "String", "support.type.rune"));
  check("`let` binds a variable", has(tokens, "label", "variable.other.declaration.rune"));
  check("format placeholders in strings", has(tokens, "{}", "constant.other.placeholder.rune") && has(tokens, "{:>8.2}", "constant.other.placeholder.rune"));
  check("escapes in strings", has(tokens, "\\n", "constant.character.escape.rune"));
  check("hex with a suffix is a number", scopesOf(tokens, "0x1F_").some((s) => s.startsWith("constant.numeric.hex")) || tokens.some((t) => t.text.startsWith("0x1F") && t.scopes.some((s) => s.startsWith("constant.numeric.hex"))));
  check("the suffix is marked", has(tokens, "u8", "storage.type.numeric.rune") || has(tokens, "i64", "storage.type.numeric.rune", 1));
  check("floats with exponents", tokens.some((t) => t.text.startsWith("2.5e-3") && t.scopes.some((s) => s.startsWith("constant.numeric.float"))));
  check("`0..count` is a range, not a float", has(tokens, "..", "keyword.operator.range.rune") && has(tokens, "0", "constant.numeric.integer.rune"));
  check("control keywords", has(tokens, "for", "keyword.control.loop.rune") && has(tokens, "return", "keyword.control.flow.rune"));
  check("nil is a constant", has(tokens, "nil", "constant.language.null.rune"));
  check("raw strings run to their own terminator", tokens.some((t) => t.text.includes("\"quoted\"") && t.scopes.includes("string.quoted.other.raw.rune")));
  check("character literals", tokens.some((t) => t.line === 16 && t.scopes.includes("string.quoted.single.rune")));
  check("intrinsics after `.$`", has(tokens, "length", "support.function.intrinsic.rune"));
  check("method calls", has(tokens, "println", "entity.name.function.call.rune") || has(tokens, "println", "entity.name.function.method.rune"));
  check("fields after `.`", has(tokens, "name", "variable.other.member.rune"));
  check("Self and self", has(tokens, "Self", "storage.type.self.rune") && has(tokens, "self", "variable.language.self.rune"));
  check("Some and None are prelude constants", has(tokens, "Some", "support.constant.core.rune") && has(tokens, "None", "support.constant.core.rune"));
  check("macro calls", has(tokens, "format", "entity.name.function.macro.rune"));
  check("SCREAMING_CASE is a constant", has(tokens, "MAX_DEPTH", "constant.other.caps.rune"));
  check("`??` is an operator", has(tokens, "??", "keyword.operator.coalesce.rune"));
  check("block comments nest", tokens.some((t) => t.text.includes("still") && t.scopes.includes("comment.block.rune")));
  check("block strings span lines", tokens.some((t) => t.text.includes("two") && t.scopes.includes("string.quoted.triple.rune")));
  check("@function is a type, not a decorator", has(tokens, "function", "storage.type.function.rune"));
  check("closure arrows", has(tokens, "->", "keyword.operator.arrow.rune") && has(tokens, "=>", "keyword.operator.arrow.rune"));
  check("@lint levels are keywords", has(tokens, "allow", "keyword.other.lint.rune") && has(tokens, "warn", "keyword.other.lint.rune"));
  check("@lint rule names", has(tokens, "unused-variable", "constant.other.lint-rule.rune") && has(tokens, "todo", "constant.other.lint-rule.rune"));
  check("`bind` and `to` are keywords", has(tokens, "bind", "keyword.other.rune") && has(tokens, "to", "keyword.other.rune"));
  check("a type alias's name", has(tokens, "Alias", "entity.name.type.alias.rune"));
  check("a global's name", has(tokens, "failed", "variable.other.global.rune"));
  check("a macro's name", has(tokens, "twice", "entity.name.function.macro.rune"));
  check("`/` after a number is division, not a comment",
        (() => { const r = tokenize(grammar, "let x = 4 / 2 // half").tokens; return has(r, "/", "keyword.operator.arithmetic.rune") && r.some((t) => t.text.includes("half") && t.scopes.includes("comment.line.double-slash.rune")); })());

  // Every file in the repository: nothing may be left open at the end.
  const files = [];
  const skip = new Set(["node_modules", "target", ".git", "build", "build-release"]);
  (function walk(dir) {
    for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
      if (skip.has(e.name)) continue;
      const p = path.join(dir, e.name);
      if (e.isDirectory()) walk(p);
      else if (e.name.endsWith(".rune")) files.push(p);
    }
  })(root);
  let open = [];
  const started = Date.now();
  for (const f of files) {
    const { state } = tokenize(grammar, fs.readFileSync(f, "utf8"));
    // The rule stack is back at the root only when every string and comment closed.
    if (state.depth !== 1) open.push(path.relative(root, f));
  }
  check(`all ${files.length} .rune files tokenise and close every string and comment`, open.length === 0, open.slice(0, 10));
  console.log(`     (${Date.now() - started} ms)`);

  console.log(failures ? `\n${failures} failure(s)` : "\nall grammar checks passed");
  process.exit(failures ? 1 : 0);
})();
