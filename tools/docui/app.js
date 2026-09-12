/* What the page does once it is on screen.
 *
 * Open and close branches of the tree, filter it, keep the entry for
 * whatever is on screen marked, and keep the address bar's hash on that
 * same entry as the reader scrolls. The tree itself is rendered by the
 * generator — this only reacts to it, so the page is complete before any
 * of this runs and stays usable if none of it does. */
(function () {
  "use strict";

  var tree = document.getElementById("tree");
  var side = document.querySelector(".side");
  var shell = document.querySelector(".shell");
  var finder = document.getElementById("finder");
  var field = document.getElementById("q");
  var empty = document.getElementById("empty");
  var toggleAll = document.getElementById("collapse");
  var findBtn = document.getElementById("find");
  var menuBtn = document.getElementById("menu");
  if (!tree) return;

  try { history.scrollRestoration = "manual"; } catch (e) {}

  var branches = Array.prototype.slice.call(tree.querySelectorAll("li.has"));
  var rows = Array.prototype.slice.call(tree.querySelectorAll(".row"));

  /*--- Opening and closing ---------------------------------------------*/

  function setOpen(li, open) {
    var kids = li.querySelector(":scope > ul");
    if (!kids) return;
    li.classList.toggle("open", open);
    kids.hidden = !open;
    var row = li.querySelector(":scope > .row");
    if (row) row.setAttribute("aria-expanded", open ? "true" : "false");
  }

  /* A branch is open for one of two reasons: the reader opened it, or it is
     on the way to whatever is on screen. The first is theirs and stays; the
     second is the page following along, and has to fold up again once it is
     no longer the way to anywhere — otherwise scrolling once leaves the tree
     open at every branch it passed. */
  var pinned = [];
  var following = [];
  var fromTree = false;

  function isPinned(li) { return pinned.indexOf(li) >= 0; }
  function pin(li) { if (!isPinned(li)) pinned.push(li); }
  function unpin(li) {
    var at = pinned.indexOf(li);
    if (at >= 0) pinned.splice(at, 1);
  }

  function openAncestors(node) {
    var wanted = [];
    for (var li = node.closest("li"); li; li = li.parentElement.closest("li"))
      wanted.push(li);
    following.forEach(function (li) {
      if (wanted.indexOf(li) < 0 && !isPinned(li)) setOpen(li, false);
    });
    following = [];
    wanted.forEach(function (li) {
      setOpen(li, true);
      if (!isPinned(li)) following.push(li);
    });
  }

  branches.forEach(function (li) { setOpen(li, false); });

  /* A click on a branch opens it as well as going there: the thing clicked is
     a heading, and what is under it is what the reader asked to see. The
     arrow is the one that pins — a name just goes there, and the branch
     folds up again once it is no longer the way to the page. */
  tree.addEventListener("click", function (e) {
    var row = e.target.closest(".row");
    if (!row) return;
    var li = row.parentElement;
    if (li.classList.contains("has")) {
      var wasOpen = li.classList.contains("open");
      var onTwist = !!e.target.closest(".twist");
      if (onTwist) {
        var open = !wasOpen;
        setOpen(li, open);
        if (open) pin(li); else unpin(li);
        e.preventDefault();
        e.stopPropagation();
        return;
      }
      setOpen(li, true);
    }
    fromTree = true;
    mark(row);
    fromTree = false;
  });

  var allOpen = false;
  function setAll(open) {
    allOpen = open;
    pinned = [];
    following = [];
    branches.forEach(function (li) {
      setOpen(li, open);
      if (open) pin(li);
    });
    if (toggleAll) {
      toggleAll.setAttribute("aria-pressed", open ? "true" : "false");
      toggleAll.title = open ? "Collapse everything" : "Expand everything";
      toggleAll.setAttribute("aria-label", toggleAll.title);
    }
  }
  if (toggleAll) toggleAll.addEventListener("click", function () { setAll(!allOpen); });

  /*--- Folding the tree away ---------------------------------------------*/

  /* The whole left side, out of the way. The button itself stays, because a
     control that hides itself cannot be undone. */
  function setFolded(on) {
    if (!shell) return;
    shell.classList.toggle("folded", on);
    if (menuBtn) {
      menuBtn.setAttribute("aria-pressed", on ? "true" : "false");
      menuBtn.setAttribute("aria-expanded", on ? "false" : "true");
      menuBtn.title = on ? "Show the contents" : "Hide the contents";
      menuBtn.setAttribute("aria-label", menuBtn.title);
    }
  }
  if (menuBtn) menuBtn.addEventListener("click", function () {
    setFolded(!shell.classList.contains("folded"));
  });

  /*--- Filtering --------------------------------------------------------*/

  function showFinder(on) {
    if (!finder) return;
    finder.hidden = !on;
    if (findBtn) findBtn.setAttribute("aria-pressed", on ? "true" : "false");
    if (on) { field.focus(); field.select(); }
    else if (field.value) { field.value = ""; filter(""); }
  }
  if (findBtn) findBtn.addEventListener("click", function () {
    showFinder(finder.hidden);
  });

  /* Words, not a single substring. `std io` finds `std::io`. Short tokens
     (three letters or fewer) have to be a whole word, so `for` does not
     light up `format`. `?`, `$`, `@` and `->` stay tokens, and `"a phrase"`
     matches those words in order. */
  function camelSplit(s) {
    return String(s)
      .replace(/([a-z0-9])([A-Z])/g, "$1 $2")
      .replace(/([A-Z]+)([A-Z][a-z])/g, "$1 $2");
  }

  function normalize(s) {
    var t = camelSplit(s).toLowerCase();
    t = t.replace(/::/g, " ");
    t = t.replace(/->/g, " -> ");
    t = t.replace(/=>/g, " => ");
    t = t.replace(/[_\-./,;:()[\]{}<>`"'=+*!]+/g, " ");
    t = t.replace(/\?/g, " ? ");
    t = t.replace(/\$/g, " $ ");
    t = t.replace(/@/g, " @ ");
    return t.replace(/\s+/g, " ").trim();
  }

  var termCache = typeof WeakMap === "function" ? new WeakMap() : null;

  function termsOf(li) {
    if (termCache && termCache.has(li)) return termCache.get(li);
    var extra = li.getAttribute("data-terms") || "";
    var label = "";
    var node = li.querySelector(":scope > .row .label");
    if (node) label = node.textContent || "";
    var kind = "";
    var k = li.querySelector(":scope > .row .kind");
    if (k) kind = k.textContent || "";
    var title = label + " " + kind;
    var out = {
      raw: (title + " " + extra).toLowerCase(),
      title: " " + normalize(title) + " ",
      all: " " + normalize(title + " " + extra) + " "
    };
    if (termCache) termCache.set(li, out);
    return out;
  }

  function parseQuery(query) {
    var phrases = [];
    var rest = String(query).replace(/"([^"]+)"/g, function (_, p) {
      var t = p.trim().toLowerCase();
      if (t) phrases.push(t);
      return " ";
    });
    return { phrases: phrases, words: normalize(rest).split(" ").filter(Boolean) };
  }

  function isPunct(w) {
    return w === "?" || w === "$" || w === "@" || w === "->" || w === "=>";
  }

  function wordHit(hay, w) {
    /* Short tokens and punctuation look at the name, not the page body —
       otherwise `for` lights up every sentence that uses the word. */
    var whole = isPunct(w) || w.length <= 2;
    var space = (whole || w.length === 3) ? hay.title : hay.all;
    if (whole) return space.indexOf(" " + w + " ") >= 0;
    return space.indexOf(" " + w) >= 0;
  }

  function matches(hay, parsed) {
    var i;
    for (i = 0; i < parsed.phrases.length; i++) {
      if (hay.raw.indexOf(parsed.phrases[i]) < 0) return false;
    }
    for (i = 0; i < parsed.words.length; i++) {
      if (!wordHit(hay, parsed.words[i])) return false;
    }
    return parsed.phrases.length > 0 || parsed.words.length > 0;
  }

  function needlesOf(parsed) {
    var out = parsed.words.slice();
    var i;
    for (i = 0; i < parsed.phrases.length; i++) {
      parsed.phrases[i].split(/\s+/).forEach(function (w) {
        if (w && out.indexOf(w) < 0) out.push(w);
      });
    }
    out.sort(function (a, b) { return b.length - a.length; });
    return out;
  }

  function rememberLabel(el) {
    if (!el.getAttribute("data-orig")) el.setAttribute("data-orig", el.innerHTML);
  }

  function restoreLabel(el) {
    var orig = el.getAttribute("data-orig");
    if (orig != null) el.innerHTML = orig;
  }

  function highlightLabel(el, needles) {
    rememberLabel(el);
    restoreLabel(el);
    if (!needles.length) return;
    var walk = document.createTreeWalker(el, NodeFilter.SHOW_TEXT, null);
    var nodes = [];
    while (walk.nextNode()) nodes.push(walk.currentNode);
    nodes.forEach(function (node) {
      var text = node.nodeValue;
      if (!text) return;
      var lower = text.toLowerCase();
      var frag = document.createDocumentFragment();
      var i = 0;
      var any = false;
      while (i < text.length) {
        var bestAt = -1;
        var bestLen = 0;
        var n;
        for (n = 0; n < needles.length; n++) {
          var w = needles[n];
          if (!w) continue;
          var at = lower.indexOf(w, i);
          if (at < 0) continue;
          if (bestAt < 0 || at < bestAt || (at === bestAt && w.length > bestLen)) {
            bestAt = at;
            bestLen = w.length;
          }
        }
        if (bestAt < 0) {
          frag.appendChild(document.createTextNode(text.slice(i)));
          break;
        }
        if (bestAt > i)
          frag.appendChild(document.createTextNode(text.slice(i, bestAt)));
        var mark = document.createElement("mark");
        mark.className = "hit";
        mark.textContent = text.slice(bestAt, bestAt + bestLen);
        frag.appendChild(mark);
        any = true;
        i = bestAt + bestLen;
      }
      if (any) node.parentNode.replaceChild(frag, node);
    });
  }

  function visibleHits() {
    return Array.prototype.slice.call(
      tree.querySelectorAll("li.hit:not([hidden]) > .row")
    );
  }

  var hitAt = -1;

  function jumpMatch(dir) {
    var rows = visibleHits();
    if (!rows.length) return;
    if (hitAt < 0 || hitAt >= rows.length) hitAt = dir > 0 ? -1 : 0;
    hitAt = (hitAt + dir + rows.length) % rows.length;
    var row = rows[hitAt];
    var href = row.getAttribute("href") || "";
    if (href.charAt(0) === "#") {
      var id = decodeURIComponent(href.slice(1));
      if (goTo(id, true)) history.replaceState(null, "", "#" + id);
    }
    fromTree = true;
    mark(row);
    fromTree = false;
    keepRowVisible(row);
  }

  function filter(text) {
    var q = text.trim();
    side.classList.toggle("searching", q !== "");
    hitAt = -1;
    var labels = tree.querySelectorAll(".row .label");
    if (!q) {
      tree.querySelectorAll("li").forEach(function (li) {
        li.hidden = false;
        li.classList.remove("hit");
      });
      Array.prototype.forEach.call(labels, restoreLabel);
      /* Back to how the reader had it, not to everything open. */
      following = [];
      branches.forEach(function (li) { setOpen(li, allOpen || isPinned(li)); });
      if (current) openAncestors(current);
      if (empty) {
        empty.classList.remove("on");
        empty.textContent = "Nothing matches.";
      }
      return;
    }
    var parsed = parseQuery(q);
    var needles = needlesOf(parsed);
    var hits = 0;
    /* Deepest first, so a parent can be kept for a child that matched. */
    var items = Array.prototype.slice.call(tree.querySelectorAll("li")).reverse();
    items.forEach(function (li) {
      var self = matches(termsOf(li), parsed);
      var kid = !!li.querySelector("li:not([hidden])");
      li.hidden = !(self || kid);
      li.classList.toggle("hit", self);
      if (self) hits++;
      if (!li.hidden && li.classList.contains("has")) setOpen(li, true);
      var lab = li.querySelector(":scope > .row .label");
      if (lab) highlightLabel(lab, self ? needles : []);
    });
    if (empty) {
      empty.classList.add("on");
      empty.textContent = hits === 0
        ? "Nothing matches."
        : hits === 1 ? "1 match" : hits + " matches";
    }
  }

  if (field) {
    field.addEventListener("input", function () { filter(field.value); });
    field.addEventListener("keydown", function (e) {
      if (e.key === "Escape") { showFinder(false); }
      else if (e.key === "ArrowDown") { e.preventDefault(); jumpMatch(1); }
      else if (e.key === "ArrowUp") { e.preventDefault(); jumpMatch(-1); }
      else if (e.key === "Enter") { e.preventDefault(); jumpMatch(1); }
    });
  }

  document.addEventListener("keydown", function (e) {
    if (/^(INPUT|TEXTAREA)$/.test(document.activeElement.tagName)) return;
    if (e.key === "/") {
      e.preventDefault();
      showFinder(true);
    } else if ((e.key === "k" || e.key === "K") && (e.metaKey || e.ctrlKey)) {
      e.preventDefault();
      showFinder(true);
    }
  });

  /*--- One page at a time ------------------------------------------------*/

  /* Each top-level entry is a page. Only one is in the document's flow at a
     time, so a package the size of a standard library is a set of pages
     rather than one endless scroll. Everything is still in the file, which
     is what keeps the filter able to see all of it. */

  var pages = Array.prototype.slice.call(document.querySelectorAll("main > .page"));
  var pageOf = {};
  pages.forEach(function (page) {
    pageOf[page.id] = page;
    Array.prototype.forEach.call(page.querySelectorAll("[id]"), function (el) {
      pageOf[el.id] = page;
    });
  });

  var shown = null;
  function showPage(page) {
    if (!page || page === shown) return;
    pages.forEach(function (p) { p.hidden = p !== page; });
    shown = page;
    pager(page);
  }

  /* The one piece of chrome a paginated document earns: where to go next. */
  var foot = document.getElementById("pager");
  function pager(page) {
    if (!foot) return;
    var at = pages.indexOf(page);
    var prev = at > 0 ? pages[at - 1] : null;
    var next = at >= 0 && at < pages.length - 1 ? pages[at + 1] : null;
    foot.innerHTML = "";
    if (prev) foot.appendChild(link(prev, "\u2190 ", "prev"));
    if (next) foot.appendChild(link(next, "", "next"));
    foot.hidden = !prev && !next;
  }
  function link(page, lead, side) {
    var a = document.createElement("a");
    a.className = "step " + side;
    a.href = "#" + page.id;
    a.appendChild(document.createTextNode(
      lead + (page.getAttribute("data-title") || "")));
    /* Two pages may be called the same thing, so the mark comes along. */
    var mark = page.querySelector("h1 .glyph");
    if (mark) a.appendChild(mark.cloneNode(true));
    if (side === "next") a.appendChild(document.createTextNode(" \u2192"));
    return a;
  }

  /* Several pages reuse short heading ids (`pages`), so a lookup prefers
     the page the click belonged to — the tree's top entry, or the page
     already on screen — and only then the first owner of the id. */
  function findId(page, id) {
    if (!id) return null;
    if (!page) return document.getElementById(id);
    if (page.id === id) return page;
    var nodes = page.querySelectorAll("[id]");
    var i;
    for (i = 0; i < nodes.length; i++) {
      if (nodes[i].id === id) return nodes[i];
    }
    return null;
  }

  function pageHint(from) {
    if (from && tree && tree.contains(from)) {
      var top = from.closest("#tree > ul > li");
      if (top) {
        var a = top.querySelector(":scope > .row");
        var href = a ? a.getAttribute("href") || "" : "";
        if (href.charAt(0) === "#") {
          var topId = href.slice(1);
          try { topId = decodeURIComponent(topId); } catch (e) {}
          if (pageOf[topId]) return pageOf[topId];
        }
      }
    }
    return shown;
  }

  function hashOf() {
    if (!location.hash) return "";
    try { return decodeURIComponent(location.hash.slice(1)); }
    catch (e) { return location.hash.slice(1); }
  }

  function syncHash(id) {
    if (!id) return;
    var want = "#" + id;
    if (location.hash === want) return;
    history.replaceState(null, "", want);
  }

  var moving = 0;
  var goGen = 0;
  function afterLayout(fn) {
    requestAnimationFrame(function () { requestAnimationFrame(fn); });
  }

  /* An anchor decides the page as well as the place. */
  function goTo(id, scroll, from) {
    var hint = pageHint(from);
    var page = (hint && findId(hint, id)) ? hint : pageOf[id];
    if (!page) return false;
    var fresh = shown !== page;
    showPage(page);
    var el = findId(page, id) || document.getElementById(id);
    var gen = ++goGen;
    syncHash(id);
    if (byId[id]) mark(byId[id]);
    function jump() {
      if (gen !== goGen) return;
      if (!el || el === page) window.scrollTo(0, 0);
      else el.scrollIntoView({ block: "start" });
    }
    if (!scroll) return true;
    /* Same page: the section is already laid out, so jump now. Waiting a
       frame lets a previous goTo, or the browser's own hash scroll, win. */
    if (!fresh) {
      jump();
      track();
      return true;
    }
    moving++;
    afterLayout(function () {
      moving--;
      jump();
      if (gen === goGen) track();
    });
    return true;
  }

  document.addEventListener("click", function (e) {
    var a = e.target.closest("a[href^=\"#\"]");
    if (!a) return;
    var id = a.getAttribute("href").slice(1);
    try { id = decodeURIComponent(id); } catch (err) {}
    if (!id) return;
    if (goTo(id, true, a)) e.preventDefault();
  });

  window.addEventListener("hashchange", function () {
    var id = hashOf();
    if (id) goTo(id, true);
  });

  /*--- Where the reader is ---------------------------------------------*/

  var byId = {};
  rows.forEach(function (r) {
    var href = r.getAttribute("href") || "";
    if (href.charAt(0) === "#") byId[href.slice(1)] = r;
  });

  var current = null;
  function keepRowVisible(row) {
    if (!row || !tree) return;
    var r = row.getBoundingClientRect();
    var t = tree.getBoundingClientRect();
    var pad = 28;
    if (r.top < t.top + pad) tree.scrollTop -= (t.top + pad - r.top);
    else if (r.bottom > t.bottom - pad) tree.scrollTop += (r.bottom - (t.bottom - pad));
  }

  function mark(row) {
    if (row === current) {
      if (!fromTree) keepRowVisible(row);
      return;
    }
    if (current) current.classList.remove("here");
    current = row;
    if (!row) return;
    row.classList.add("here");
    openAncestors(row);
    if (!fromTree) keepRowVisible(row);
    var href = row.getAttribute("href") || "";
    if (href.charAt(0) === "#") {
      var id = href.slice(1);
      try { id = decodeURIComponent(id); } catch (e) {}
      syncHash(id);
    }
  }

  /* The last named place whose top has crossed the reading line. Sections
     are included: a folder is several written pages in one article, and
     each of those pages is a section with a tree entry of its own. Picking
     the topmost intersecting container would stick on the first of them. */
  function anchorsOn(page) {
    if (!page) return [];
    var out = [];
    var nodes = page.querySelectorAll("[id]");
    var i;
    for (i = 0; i < nodes.length; i++) {
      if (byId[nodes[i].id]) out.push(nodes[i]);
    }
    return out;
  }

  function currentAnchor() {
    var list = anchorsOn(shown);
    if (!list.length) return null;
    var line = Math.max(72, window.innerHeight * 0.12);
    var best = list[0];
    var i;
    for (i = 0; i < list.length; i++) {
      if (list[i].getBoundingClientRect().top <= line) best = list[i];
      else break;
    }
    return best;
  }

  function track() {
    if (moving) return;
    if (side && side.classList.contains("searching")) return;
    var el = currentAnchor();
    if (!el || !byId[el.id]) return;
    mark(byId[el.id]);
  }

  var trackTimer = 0;
  function trackSoon() {
    if (trackTimer) return;
    trackTimer = requestAnimationFrame(function () {
      trackTimer = 0;
      track();
    });
  }

  window.addEventListener("scroll", trackSoon, { passive: true });

  /* Arriving with an anchor already in the address bar opens that page.
     Without one, the first page. */
  var start = hashOf();
  if (!start || !goTo(start, true)) {
    showPage(pages[0]);
    track();
  }
})();
