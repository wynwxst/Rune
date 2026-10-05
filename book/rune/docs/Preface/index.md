# Rune
A systems programming language designed to be reliable, have as much functionality as possible and keep user compilation simple.

## Why?
Have you ever disagreed with a language's syntax, or thought this could be a lot simpler/ better done? Over the years in programming many different languages (interpreted and compiled), many such thoughts were accumulated. Rune sets out as a language that still has the joy of being able to do anything, done simply and with every complication optional to the user, whether it suits their problem.

As you grow in your programming journey, you will often drift from working in a specific programming language to: "Use the right language for the right problem". Rune aims to solve a series of problems governing:

- Coding projects (readable, optionally maintainable, documentable with standards and enforced design patterns)
- Embedded/Bare metal programming (ease of compilation to other targets and facilitated abstractions)
- Tool based scripting
- memory safe interface over other languages
- & more!

However one emphasis that should be expressed, is that unlike new trending languages, Rune is not some flashy way of solving a problem but a stable and readable clean foundation around programming itself that is based on well known solutions with a **Rune spin on it**; the language itself is designed to be extendable based on what is required

## This language sounds like a *lot* of moving parts
In some ways yes, Rune adopts a very chimera-like approach and can be used many different ways. However the core design philosophy is simplicity with optional complexity. This means that it is very simple to change options and for those that want to configure from scratch, this option is avaliable to them.

## Enough about philosophy, what kind of syntax and memory management?
Rune's syntax is a hybrid of Rust and Swift. The design choices behind this will be explained later on. The memory management is largely automatic (Automated reference counting or Borrow/Ownership Checking), however manual memory management is allowed.

## What advantages does Rune have over other languages?
It's compact, fast and new. Unfortunately a lot of older languages have partial bloating in their standard library or features which aren't intuitive. Rune immediately works with a couple questions when contributing:

- Is this addition neccessary or can it be split into something optional (if even present)?
- Is this a compiler or standard library feature?
  - If compiler, can this be made concurrent? Is this fast enough and uses proper data structures/ algorithms?
  - If standard library, is this a zero cost abstraction?
- Will this introduce further dependencies on other libraries/codebases?
  - If so, should it be made optional or standalone for the user to get?
- Does this addition increase or decrease simplicity?

## This sounds oddly complicated
Try it and see! It's a lot of words but no code... yet; so continue onwards!
