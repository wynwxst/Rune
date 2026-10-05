# margrave

`grve` is a terminal workspace written in Rune: a file explorer, an editor with
syntax highlighting and language-server support, splits divided by thin lines,
and shell commands whose output opens along the bottom. It never paints a
background — the window stays your terminal's own.

```sh
cd examples/margrave
rune build --release
ln -s "$PWD/target/release/grve" ~/.local/bin/grve   # or anywhere on PATH
```

```sh
grve              # the current folder, in the explorer
grve src/         # a folder
grve src/main.rune  # a file, in the editor
```

## Keys

Every chord starts with the **prefix**, which is Option/Alt unless
`[keys] prefix` says otherwise (`alt`, `ctrl` or `super`). Option is read through
the kitty keyboard protocol where the terminal speaks it, and from the characters
macOS types (`ç` is Option+C) where it does not — so it works in Terminal.app
without "Use Option as Meta" too. `alt+shift+h` (or `?` in the explorer) shows
everything below inside the program.

| Everywhere | |
| --- | --- |
| `alt+arrows` | move between panes (and the shell output) |
| `alt+;` | the command line (plain `:` in the explorer) |
| `alt+?` | every command and chord, with the prefix in force (↑↓ scroll it) |
| `alt+/` | search (plain `/` in the explorer) |
| `alt+a` | stop the running shell command |
| `alt+q` | close the pane; the last one quits |
| `alt+\` / `alt+-` / `alt+=` | split right / split below / even the splits |
| `alt+9` / `alt+0` | back / forward through the files and jumps this pane has been through (opening a file, go to definition, references, symbols, `:<line>`) |

| Explorer | |
| --- | --- |
| `up` `down` `pgup` `pgdn` `home` `end` | move |
| `enter` | open a folder or file — or run the file's action |
| `right`, `e` / `left`, `backspace` | open without the action / up a folder |
| `o` / `s` | open in a split on the right / below |
| `!` `:` `/` | shell command, command line, search names |
| `.` `r` `y` `~` `n` `d` `g` | hidden files, refresh, copy path, home, new file, new folder, go to |
| `esc` | back to the file the pane had open |
| mouse | click selects, double click opens, the wheel moves |

| Editor | |
| --- | --- |
| `shift+arrows` | select |
| `alt+shift+arrows` | jump: a word left/right, a paragraph up/down |
| `alt+c` `alt+x` `alt+v` | copy, cut, paste (the whole line when nothing is selected) |
| `alt+]` `alt+[` | indent, dedent |
| `alt+delete` | delete the line (or every line the selection touches) |
| `alt+s` `alt+z` `alt+y` | save, undo, redo |
| `( [ { " ' \`` | close themselves; typing the closer steps over it, backspace in an empty pair removes both, and with a selection they wrap it (`[editor] auto_pairs`) |
| `tab` | complete — a dropdown (arrows, `tab`/`enter` to take it, `esc` to close); indents at the start of a line |
| `alt+/` | search: a regex, smart case, highlighted as you type; `left`/`right` go between hits, `enter` selects the hit, `esc` leaves; `alt+j`/`alt+p` next/previous later |
| `alt+g` | go to line |
| `alt+enter` | the same file again, split right |
| `esc` | to the explorer |
| mouse | click moves the cursor (and focuses the pane clicked), drag selects, shift+click extends, double click selects a word, the wheel scrolls |

| Language server | |
| --- | --- |
| `alt+l` | the language menu: everything below, in a dropdown |
| `alt+d` `alt+k` `alt+r` `alt+o` | definition (jumps, then shows its documentation), documentation, references, symbols |
| mouse | resting on a name shows its documentation, without moving the cursor |

Documentation is a panel built from the doc comment at the definition: the
signature in colour, then the whole comment (`///`, `//`, `#`, `/** */`, or a
Python docstring), with `code` picked out. The next key puts it away and does
nothing else; a panel the mouse brought up goes when the mouse leaves the name
and lets typing through. A name with no doc comment shows the language
server's hover instead.
| `alt+f` `alt+.` `alt+n` `alt+e` | format, code actions, rename, the line's diagnostics |
| `alt+shift+d` `alt+shift+r` `alt+shift+o` | definition, references, symbols — opened read-only in a **peek** panel along the bottom (the same 70/30 split as shell output) instead of the pane: move and scroll in it, `/` searches from its cursor, `n`/`N` step through hits, `enter` closes it |

While a call is being typed, its signature sits above the line with the
argument in hand picked out (`(` and `,` bring it up; `esc` or leaving the call
puts it away).

Errors are checked live, without saving: once typing pauses for about a
quarter of a second the text goes to the server, which checks it after its own
short pause. While a line is being typed, its out-of-date marks stay hidden;
marks elsewhere move with their lines. Errors turn the offending code red and
underline it; warnings underline it in yellow. The line number takes the colour too, the message follows the line,
a problem past the end of a line (a missing `}`) is marked with `‸`, and with
the cursor on the line the status line shows the whole message. Without a server, `tab` completes from words in the file.

## Commands

| | |
| --- | --- |
| `:g <path>` | go to a folder (or open a file) here; lists what is there as you type |
| `:o <path>` / `:sp <path>` | open a file or folder in a split on the right / below |
| `:e <path>` | edit a file here (a new path makes a new file) |
| `:sh <command>` | run it in the working directory; `%` is the current file |
| `:w [path]` `:wa` `:q` `:q!` `:wq` `:qa` `:qa!` | write and quit |
| `:cd <dir>` `:mkdir <dir>` `:new` `:only` | |
| `:set tab=4` `tabs` `spaces` `numbers` `relative` `hidden` `guides` (and `no…`) | |
| `:theme <name>` | switch colour scheme (completes the names) |
| `:noh` `:format` `:lsp [restart]` `:reload` `:config` `:help` `:<line>` | |

Paths are relative to the working directory — the folder `grve` was started
in — and `~` is home. Only `:cd` changes it: `:g`, `:o`, `:sp`, `:e`, go to
definition and the explorer move what a pane shows, never where paths start.
File actions run in the file's own folder. Shell commands
take the bottom 30% of the window (`[shell] output_height`); the output
streams in while they run, `alt+a` stops them (the whole process group), and
`enter` dismisses the output once they are done.

## Configuration

`~/.config/mgrve/` (or `$XDG_CONFIG_HOME/mgrve/`), written with comments on the
first run. A file that does not parse is reported on the status line and the
defaults stand in for it; `:reload` reads them again.

- **config.toml** — `[editor]` tab width (2), spaces or tabs, line numbers,
  scroll margin, indent guides, `mouse` (on; while it is, Shift-drag — Option
  in iTerm2 — gives the terminal's own text selection); `[keys]` prefix; `[explorer]`; `[shell]`;
  `[clipboard]` copy/paste commands (pbcopy/pbpaste, wl-copy, xclip; OSC 52 when
  there are none); `[theme]`: `name` picks a scheme — `margrave` (default),
  `rose`, `nord`, `gruvbox`, `mono` (greys; colour only for problems and
  hits) or `paper` (for light terminals) — and any colour set beside it, or
  under `[theme.syntax]`, overrides the scheme's. None paints a background
  but the selection. `:theme <name>` switches while running.
- **actions.toml** — what `enter` does to a file in the explorer:

  ```toml
  [[action]]
  match = ['*.tar.gz', '*.tgz']
  run = 'tar -xvf {}'        # {} path, {name}, {dir}, {stem}; all quoted
  confirm = false            # true asks first

  [[action]]                 # by what the file is, not its name
  magic = ['macho-exec', 'elf-exec']
  run = '{}'                 # run the program (the default does this)
  confirm = true
  ```

  `magic` reads the file's first bytes: `macho`, `macho-exec`,
  `macho-dylib`, `macho-universal`, `elf`, `elf-exec`, `elf-shared`, `pe`,
  `script`, `wasm`, `pdf`, `png`, `jpeg`, `gif`, `zip`, `gzip`, `xz`,
  `bzip2`, `java-class` and `executable` (permission bits), or a hex prefix
  such as `'0x7f454c46'`. An action with both `match` and `magic` needs both.
  A config directory made before this existed has no program action; add
  the block above to its `actions.toml`.

- **lsp.toml** — servers by language; `rune lsp` is on by default, and a
  server that is not installed is skipped quietly.
- **languages.toml** — syntax definitions beyond the built-in ones (rune, c,
  python, javascript, rust, go, shell, toml, json, yaml, markdown, lua, make,
  cmake).

An existing file's indentation is detected when it is opened; new files get
the configured default.

Long lines wrap (`[editor] wrap = true`, or `:set wrap` / `:set nowrap`):
they break after a space where one falls late enough in the row, mid-word
otherwise, and the rows after the first hang at the line's own indent. Up and
down move a row at a time, keeping the column; clicks, the wheel, search hits,
diagnostics and popups all follow the rows. With wrapping off, long lines
scroll sideways instead.

## How it is built

| File | |
| --- | --- |
| `c/sys.c` | the only C: raw mode, `poll`, children on pipes, POSIX regex, `wcwidth` |
| `src/sys.rune` | the checked edge over it — nothing else says `unsafe` |
| `src/term.rune`, `src/input.rune` | the cell grid (diffed, synchronized output) and key decoding |
| `src/toml.rune`, `src/config.rune` | a TOML 1.0 reader, and the settings built from it |
| `src/buffer.rune`, `src/highlight.rune` | documents with grouped undo; table-driven highlighting |
| `src/lsp.rune`, `src/glue.rune` | JSON-RPC to the servers; what the editor does with replies |
| `src/app.rune`, `keys.rune`, `commands.rune`, `view.rune` | the workspace, split by concern with `extend App` |

It is built with `memory = "arc"`: panes, documents and servers point at each
other freely, which is what reference counting is for. `rune test` covers the
TOML reader, the key decoder, the document model, the configuration and the
command line.
