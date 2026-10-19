# The sidebar

A design note for replacing the current sidebar with something worth having.
Nothing here is implemented yet.

## What is there now

The window is a split view:

```
AdwApplicationWindow
└── AdwOverlaySplitView
    ├── content ──► AdwTabOverview
    │                 ├── view  ──► AdwTabView   (the tab bar and the web views)
    │                 └── child ──► the header bar
    └── sidebar ──► EphyBookmarksDialog
```

The sidebar is `EphyBookmarksDialog`, a widget that was written to be a dialog
and is being used as a panel. It is in `src/bookmarks/ephy-bookmarks-dialog.c`
and it is the sidebar's only user: nothing else in the tree opens it.

What it shows is a flat list of the bookmarks bar, a search field, and a list
of tags. The list is the bookmarks bar because that is what `bookmarks_order`
holds, and the order of the rows is written straight back into it
(`update_bookmarks_order()`), which is also what syncs with Firefox's toolbar
bookmarks.

The sidebar is toggled by a window action, and `show_sidebar_cb()` disables a
list of window, toolbar and tab actions while it is open.

## Why it is not what we want

Three problems, in order of how much they matter.

**The list is flat.** A browser sidebar is a tree. Epiphany's local bookmark
model has no folders: a bookmark has a URL, a title, some tags and an icon.
Groups are tags, which are flat strings, and the tags are not hierarchical
either.

**A dialog is doing a panel's job.** The widget carries editing affordances,
tag management, and a search mode that are all reasonable in a dialog and
mostly noise in a panel that sits next to the page you are reading.

**Nothing is remembered.** The sidebar starts closed, every time, in every
window, at whatever width the split view happens to pick.

## The design

### Shape

One panel, in the pane the sidebar already occupies. Three sections, in this
order, separated by hairlines:

```
┌──────────────────────┐
│ Search…              │  a search field, always visible
├──────────────────────┤
│ Bookmarks bar        │  the flat bar, as it is today
├──────────────────────┤
│ Tags                 │  the tree, one section per tag
│  ▸ Work              │
│  ▸ News              │
│  ▸ Reading           │
├──────────────────────┤
│ + Add bookmark       │
└──────────────────────┘
```

The bar stays flat because it is the bookmarks bar, and it is flat in Firefox
too. The tree below it is where the grouping happens.

### The tree problem

This is the decision that shapes everything else, so it is worth being
explicit about the options.

**Option A: build the tree on tags.** A tag becomes a section, and a
bookmark with two tags appears under both. Nothing changes in the storage
format, the sync format, or the Firefox compatibility. The cost is that tags
are flat, so a section cannot contain another section, and a bookmark in
three tags appears three times. The tags are already a first-class concept
in the bookmark manager and the sync code, so this is the small change.

**Option B: add real folders.** A bookmark gains a parent, the tree becomes
a real tree, and it looks like every other browser. The cost is a change to
the local storage format, a change to the sync payload, and a migration for
existing bookmarks, plus the work of deciding how a bookmark with tags and a
folder is displayed. Firefox Sync already sends `parentId`, so the wire
format is not the problem; the local database is.

**Option C: no tree at all.** A compact list of recently used and pinned
bookmarks, the way a phone or a launcher does it. Much less work, and the
thing most people would actually use.

Option A is the one to build first because it is a few hundred lines and
touches nothing that would need migrating. Option B is the one that ends up
looking like Zen, and it is a separate piece of work with a migration
attached. Option C is worth keeping in mind as the fallback if A turns out
to be fiddly.

**Open question: which one?**

### What a bookmark can be

`EphyBookmark` already has a `load_in_sidebar` property. It comes from
Firefox Sync, where a bookmark can be marked to appear in the Firefox
sidebar, and Epiphany stores it and never uses it.

That is the natural home for "this bookmark belongs in the sidebar", and
using it means a bookmark that is in the Firefox sidebar shows up here too.
The alternative is a new local flag, which would not sync.

**Open question: do we use `load_in_sidebar`, or do we make our own list?**

### Search

The dialog already has a search that filters as you type, over the URL and
the title. It should be reused rather than rewritten, and it should search
the tree, not just the bar, so that a bookmark in a tag can be found by
typing part of its name.

### Settings

Two new keys in the `com.sparkybin.Browser.ui` schema, next to
`tabs-bar-visibility-policy`:

- `sidebar-visible`, a boolean, so the sidebar comes back the way you left
  it
- `sidebar-width`, an integer, so the width is yours rather than the split
  view's

Whether the state is per window or shared across them is a question. A
setting suggests shared; a window that remembers its own width suggests per
window. The obvious answer is shared visibility, per window width, which is
what Firefox does.

## What phase 1 does not do

- **No vertical tabs.** That is phase 2, and it is a different piece of work
  with a different risk profile. See below.
- **No real folders**, unless the answer to the tree question is option B.
- **No change to the tab overview.** The grid of tab thumbnails is a
  different affordance from a list of tabs, and both can exist.
- **No change to the bookmarks bar model.** The bar keeps its flat shape and
  its Firefox sync behaviour.

## Phase 2: vertical tabs

Zen puts vertical tabs in the left pane, with bookmarks as one tab of a
notebook beside them. We already use the left pane for bookmarks, so vertical
tabs would either take that pane or share it.

The obstacle is not the layout, it is that `AdwTabView` draws a horizontal
tab bar over the model a vertical strip would show. A vertical tab list means
building a `GtkListView` over the tab model and keeping it in step with
`AdwTabView` for selection, reordering, closing and the context menu. That
is the kind of work that browsers like Zen and Vivaldi do as part of forking
the tab system rather than as a skin on top of it.

So the order matters. Build phase 1, live with it, and only then decide
whether the vertical tab list is worth the risk. If it is, the left pane
becomes a notebook with Tabs, Bookmarks and History in it, and the sidebar
built in phase 1 becomes one of those pages.

## Files

New:

- `src/sidebar/` — the panel, its rows, and the tree model
- `src/sidebar/ephy-sidebar-panel.c` — the widget that replaces
  `EphyBookmarksDialog` in the split view
- `src/sidebar/ephy-sidebar-tree.c` — the tree over tags, or over folders
  if that is the answer

Changed:

- `src/ephy-window.c` — put the new panel in the split view
- `src/bookmarks/ephy-bookmarks-dialog.c` — keep it for managing bookmarks,
  and give it a home of its own, probably a preferences page
- `data/com.sparkybin.browser.gschema.xml` — the two new keys
- `src/resources/gtk/` — the panel's Blueprint

## Open questions

1. **Which tree option?** Tags, real folders, or no tree.
2. **Does the sidebar show the bookmarks bar, or replace it with a flat list
   of everything?** Today the bar is the sidebar. If the tree carries the
   grouping, the bar is redundant, and Firefox users would notice it going.
3. **Is the sidebar a panel, or a notebook?** A notebook with Bookmarks,
   History and Tabs in it is where Zen is heading, and it would make phase 2
   cheaper. But with one page in it, a notebook is a lot of furniture.
4. **Should it be the default?** The sidebar is closed at the moment. A
   sidebar that is genuinely useful might be open by default, which changes
   the look of the browser for everyone.
