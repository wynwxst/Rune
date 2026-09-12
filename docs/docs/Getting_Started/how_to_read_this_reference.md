# How to read this reference

This tree is the language reference split by category and heading. Every code sample comes from the same source as the HTML reference (`docs/reference/content.py`): complete programs that compile and run, declarations type-checked with a trivial `main` appended, and fragments that are grammar, shell, or manifest text rather than Rune.

| In this tree | Is |
| --- | --- |
| a folder | one category |
| a markdown file | one heading in that category |
| a caption above a code block | what the sample is showing |
| a `rune` fence | Rune source |
| a `sh` fence | a shell command |
| an `ebnf` fence | grammar |
| a `[!NOTE]` / `[!WARNING]` callout | a labelled aside |

The HTML build in `docs/reference/build.py` still compiles every sample against `runec` when it generates `docs/rune-reference.html`.
