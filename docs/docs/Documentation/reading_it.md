# Reading it

One file, and a tree down the left of it. Every top-level entry — each written page, each folder of them, each module — is a page of its own, and one is on screen at a time, with a link to the next at the foot. There are no numbers: clicking an entry opens what is under it and goes there. A page of prose expands to its `##`, `###` and `####` headings, nested as they nest in the page. Backticks in a heading become code in the tree and in the title, rather than showing the marks.

The tree follows the reader. Branches open as what they name comes into view and fold up again once it has passed — except the ones opened deliberately, which stay exactly as they were left.

| Control | Does |
| --- | --- |
| the menu icon | folds the whole tree away, and brings it back |
| the fold icon | opens every branch, or closes every branch — and holds them there while you scroll |
| the search icon, `/`, or `Ctrl+K` | reveals the filter; matching branches open as you type |
| `Enter` / `↓` / `↑` | walks the matches, opening each as it goes |
| `Esc` | closes the filter and restores the tree |

The filter is words together, not a single string: `std io` finds `std::io`, and `?` finds the page that names it. Three letters or fewer have to be a whole word, so `for` does not light up `format`. Short tokens match a name, not the page body. Quotes hold a phrase together: `"converts the error"`. Matches are marked in the tree.

The arrow beside a name is what pins a branch open. Clicking the name itself goes there, and the tree folds back to the path of whatever is on screen — so scrolling through the document does not leave every visited folder standing.

Two entries may be called the same thing — a package’s library and the program it builds are both `json` — so what tells them apart is a mark beside the name rather than a suffix bolted onto it. The same mark appears beside the heading and in the link at the foot of the page.

| Mark | Means |
| --- | --- |
| a page | something written by hand, under `docs/` |
| a folder | a directory of written pages |
| layers | the library the package builds |
| a prompt | the program it builds |
| brackets | a module inside either |

A link between two written pages becomes a link within the document: `[usage](guide/usage.md)` reaches that page’s section, because there is only ever one file.
