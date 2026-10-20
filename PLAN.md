# SparkyBin Browser — plan

Working notes. Not a press release, not a design document. This file exists
so the to-do list lives in one place instead of in a chat window.

Status as of 2026-10-20.

Decisions taken:

- Releases so far are **0.1** and **0.2**. 1.0 waits for a stable channel of
  our own; the icon and the screenshots are done.
- The website lives in its own repository, `camroybal/SparkyBin-site`.
- People install from **GitHub Releases**. A flatpak repository of our own
  comes next. Flathub is a long way off, if it happens at all.
- The eight verbose commits from 2026-10-15 have been reworded.

---

## 1. Where things stand

The tree is a fork of GNOME Web (Epiphany) 51.1, rebranded and pushed to
GitHub. Everything committed so far is on `origin/main`.

What exists:

- The rebrand, done properly: app ID `com.sparkybin.Browser`, binary
  `sparkybin-browser`, D-Bus names, GSettings schema, GResource prefixes.
- A WebExtension audit that found and fixed real crashes, use-after-frees and
  security holes (JavaScript injection in `downloads.search()`, a downloads
  path traversal, API injection into cross-origin frames).
- Documentation: `NOTICE` records the fork's changes for GPLv3 5(a),
  `HACKING.md` has working build and test recipes, `src/webextension/README.md`
  explains how the extension implementation fits together, and there is a help
  page for extensions.
- The sidebar, which shows the bookmarks or the open tabs with a switch
  between them, and fifteen colour palettes.
- The website at <https://sparkybin.com>, in its own repository.

What does not exist yet:

- A flatpak repository of our own, and the infrastructure for it: a signing
  key, somewhere to serve the OSTree tree, and something that exports and
  signs each release. Until then updates mean downloading a bundle by hand.
- The sidebar as a tab strip. The sidebar can show the open tabs now, but the
  tab bar is still the tab bar, and whether one replaces the other is open.
- A cache setting. WebKit's default is the one that uses the most memory,
  and the user is not offered a choice. See `docs/performance.md`.

---

## 2. Release types

Three build profiles already exist in `meson.build` via `-Dprofile=`. Map the
release channels onto them rather than inventing a new mechanism.

| Channel | App ID | Built from | WebKit | Updates | Where it lives |
|---|---|---|---|---|---|
| Stable | `com.sparkybin.Browser` | a release tag | GNOME runtime | on release | GitHub Releases |
| Beta / RC | `com.sparkybin.Browser` | an rc tag | GNOME runtime | as needed | GitHub Releases, marked pre-release |
| Devel | `com.sparkybin.Browser.Devel` | `main` | GNOME runtime | continuous | a flatpak repo |
| Canary | `com.sparkybin.Browser.Canary` | `main` | WebKit `main` | continuous | a flatpak repo |

Beta and RC are a pre-release flag on the stable app ID, not a separate app
ID. That keeps it to three app IDs and three manifests.

What each one is for:

- **Stable** is what most people install. It should be boring: one app ID, one
  flatpak repository, updated a handful of times a year, and built from a tag
  that matches the release notes.
- **Devel** tracks `main` against the WebKit in the GNOME runtime. This is
  what a contributor or an early adopter runs.
- **Canary** tracks `main` against WebKit `main`. It breaks more often and
  sometimes cannot start at all. It exists so that WebKit regressions get
  caught by people who opt into them.

### 2.1 Manifests need renaming

The filenames do not match the app IDs they build, which will bite whoever
publishes the stable build.

Today:

- `com.sparkybin.Browser.json` builds `com.sparkybin.Browser.Devel`
- `com.sparkybin.Browser.Canary.json` builds `com.sparkybin.Browser.Canary`

Proposed:

- `com.sparkybin.Browser.json` → builds `com.sparkybin.Browser` (stable, new)
- `com.sparkybin.Browser.Devel.json` → builds `com.sparkybin.Browser.Devel`
  (renamed from `com.sparkybin.Browser.json`)
- `com.sparkybin.Browser.Canary.json` → unchanged

The stable manifest must not pass `-Dprofile=`, otherwise the app ID picks up
the suffix.

Renaming breaks two things that need updating in the same commit:

- `.gitlab-ci.yml` (`MANIFEST_PATH`, `APP_ID`)
- `README.md`, which tells people to open the repo in GNOME Builder and pick
  a manifest by name

### 2.2 Getting a release out

1. Pick a version. The fork should start its own line rather than shipping
   `51.1`; `1.0` is a reasonable first number but it is a decision, not a
   fact.
2. Tag it. `git tag -a v1.0 -m "..."`.
3. Add a `NEWS` entry. `data/generate-release-notes` parses `NEWS` into the
   AppStream `<releases>` block, and the script only recognises release
   headers in the GNOME format, so the entry has to match that.
4. Build the stable flatpak and attach it to a GitHub Release.
5. Export it into our own flatpak repository, once there is one.

`.github/workflows/release.yml` builds the flatpak on a tag and attaches it
to a GitHub Release. `.github/workflows/ci.yml` builds and tests every push.
The CI inherited from GNOME is still in the tree, pointed at
nightly.gnome.org. The website reads the releases API when it builds, so the
download page picks up a tag on the next deploy.

One asymmetry worth knowing before choosing where to run what. The existing
`canary` job builds WebKit from `main` and takes up to four hours, on a runner
tagged `webkitgtk` that only GNOME's own infrastructure provides. The stable
and Devel builds use the WebKit already in the GNOME runtime and are cheap.
So the plan that falls out of this:

- Stable and Devel builds move to GitHub Actions, where the tag-triggered
  release workflow lives.
- The Canary stays on GitLab CI, or moves to a self-hosted runner with the
  same GNOME runtime images. Either way it is not a GitHub Actions job.

### 2.3 No infrastructure is needed to start

GitHub Releases is a file attachment, not a service to run. A
`flatpak build-bundle` produces a single `.flatpak` file that a user installs
with one command, and the runtime it needs is the GNOME runtime that is
already on their system or that they get from Flathub.

The stable channel is served that way today. Updates are manual: download the
next bundle and install it over the old one, and the profile is kept.

### 2.4 A flatpak repository of our own

A flatpak repository is an OSTree tree with an index, served over HTTP. It
does not need a server of its own: a directory published to GitHub Pages is a
working `flatpak --user remote-add` target. What it does need is a signing
key, kept out of the repository, and a step in the release job that exports
the build with `flatpak build-export`, signs the summary with
`flatpak build-update-repo --gpg-sign`, and publishes the result. The same
tree can carry the Devel and Canary profiles under their own application IDs.

Pages allows a site of 1 GB, which is more than an application this size
needs; the runtime comes from Flathub either way. Object storage behind a CDN
is the alternative if Pages runs out, and it costs a little.

Flathub is not part of this. It wants a development history and a
verification token, and neither exists yet. If it is attempted later, the
application ID is already the right shape: `com.sparkybin.Browser` is
`{tld}.{vendor}.{product}`, the domain is ours, and the `.well-known/
org.flathub.VerifiedApps.txt` file on the site would be all the verification
needs. Nothing today depends on that.

---

## 3. Website

Astro, static, on GitHub Pages.

### 3.1 Shape

- `camroybal/SparkyBin-site`, separate repository. The browser repo is
  already large and mostly C; a site does not belong inside it.
- Astro 5 with Tailwind 4. Static output, no server, no client framework.
- Deploy from a GitHub Actions workflow on push to `main`. `astro build` plus
  `actions/deploy-pages` is all it takes.
- Pages: home, download, features, screenshots, source, about, 404.
- Copy lives in Markdown files, one per page, so it can be edited without
  touching a component.

### 3.2 The download page

The point of the download page is to get people to a binary, so it should not
be where binaries live. It links out to GitHub Releases.

- Stable: `https://github.com/camroybal/SparkyBin-Browser/releases/latest`
- Every release: `https://github.com/camroybal/SparkyBin-Browser/releases`
- Canary and Devel: the flatpak repo URL, once there is one
- Source: the repository

The version number shown on the page can be read from the GitHub API at build
time, so it never goes stale. It needs a fallback for when the API is rate
limited or the site is built offline.

### 3.3 Making it look good

The site is the project's front door, so it should look considered. What that
means concretely:

- Real screenshots of the browser, taken here. The current `data/screenshot.png`
  and the two `help/C/media/epiphany-*.png` files show the upstream interface,
  which is both inaccurate and a poor advertisement.
- A wordmark in the site, not just the placeholder icon, so the design does
  not have to be redone when the icon is.
- A type scale that is not the default. The web stack everyone reaches for
  first reads as a template. A self-hosted variable font is better than a
  Google Fonts request, both for the look and for the privacy of anyone who
  loads the page.
- Light and dark, following `prefers-color-scheme`.
- Motion that respects `prefers-reduced-motion`.

---

## 4. README

The current README is the upstream one with the names changed. It reads fine
but does not tell a newcomer what the project is or how to get it. Missing:

- What the browser is, in two sentences, and where it came from
- Screenshots
- A downloads section, pointing at the same links as the website
- What is different from GNOME Web. This is the fork's entire reason to
  exist and it is currently only in `NOTICE`
- A project status line: based on Epiphany 51.1, WebExtension support is
  incomplete
- Where to report bugs. It says "the project's own tracker" and then does
  not say where that is
- A link to `HACKING.md` for building and `CONTRIBUTING.md` for reporting

`CONTRIBUTING.md` also still says "SparkyBin Browser's GitLab" in the
crashes section. The project is on GitHub.

---

## 5. Keeping AI out of the public text

People notice when reference material and commit history are machine
written, and it costs credibility with the upstream project and with
contributors. Practical rules:

**Commit messages.** Use the GNOME house style, which is terse:

```
webextension: Don't dereference a NULL WebKitWebExtension

parse_manifest() checked the GError but not the object, and get_icon()
dereferenced it unconditionally. WebKit returns NULL without setting an
error for a manifest it cannot parse.
```

A subject line naming the area and the change, then a paragraph saying what
was wrong and what the fix is. Not a bulleted changelog with bold headings.

**Prose.** No emoji. No "modern", "cutting-edge", "seamless", "powerful",
"beautiful". No "whether you're a developer or a power user". No lists of
three adjectives where one will do. Short sentences. Concrete claims that can
be checked.

**Process.** Draft with whatever is convenient, then rewrite by hand before
it lands. Every user-visible sentence gets read by a person who is willing to
say it out loud.

**The history.** The eight commits from 2026-10-15 are 200 to 600 words each,
with bold headings and bulleted lists. They are the loudest tell in the
repository. They can be reworded with an interactive rebase, which means
force-pushing `main`. The fork is young and nobody has branched from it, so
the risk is low, but it is a deliberate choice rather than something to do
quietly.

---

## 6. Triage

### P0 — Wrong or broken right now

- [x] The search provider spawned `epiphany`, which this fork does not
      install. Fixed: it goes through the desktop file, so the Canary
      wrapper is honoured too.
- [x] `browser.runtime.getBrowserInfo()`, the user agent and four warnings
      still said "Epiphany". Fixed. The WebDriver application name, the
      sync service client name and a few log messages went with them.
- [x] `CONTRIBUTING.md` refers to GitLab.
- [x] `data/screenshot.png` shows the real interface now, taken by running the
      browser under Xvfb in a container. The two `help/C/media/epiphany-*.png`
      files still show the upstream interface.
- [x] The icon is drawn: a compass with a lightning bolt for a needle, in
      the app, in the help, in the AppStream metadata and on the site.
- [x] The contact address is gone. There is no email; the issue tracker is
      the way to reach the project, and the metadata says so.
- [x] The README does not say where the bug tracker is.

### P1 — Needed before a release

- [x] Our own version number. 0.1, and 0.2.
- [x] The `stable` flatpak manifest, and the manifest renames in 2.1.
- [x] A GitHub Actions release workflow that publishes to GitHub Releases.
- [x] The `NEWS` entry and the AppStream `<releases>` block. The upstream
      release history moved to `NEWS.upstream`, because a fork's own version
      line does not sort with the versions it forked from, and AppStream
      requires the releases to be in order. The `<releases>` block is
      generated out of `NEWS` by `data/generate-release-notes`, and
      `--check` says whether the two agree.
- [x] Decide the application ID. See 2.4. It is `com.sparkybin.Browser`.
- [x] Cut the tag. v0.1 is published at
      <https://github.com/camroybal/SparkyBin-Browser/releases/tag/v0.1>.
- [x] Cut the tag for 0.2. It is published at
      <https://github.com/camroybal/SparkyBin-Browser/releases/tag/v0.2>,
      with the flatpak built against the GNOME 51 runtime. The version,
      the `NEWS` entry and the AppStream `<releases>` block are generated
      from one another, and `data/generate-release-notes --check` says
      whether they agree.
- [ ] A flatpak repository of our own. The release workflow produces a
      bundle; the repository needs a signing key, somewhere to serve the
      OSTree tree, and a step that exports and signs each release. Flathub
      is a later question, and not a near one.
- [x] Real icon and screenshots. The help screenshots were taken again
      from the browser itself, because the old ones were GNOME Web from
      2020.

### P2 — Website

Done, in `camroybal/SparkyBin-site`. Astro, static, deployed to GitHub
Pages by a workflow on push, and served from <https://sparkybin.com> over
HTTPS with a certificate GitHub issued for the domain.

- [x] Astro scaffold, GitHub Pages, deploy workflow
- [x] Home, download, source, about and 404 pages
- [x] The channel table from 2, wired to the releases page
- [x] Fonts, palette, light and dark, motion
- [x] A theme toggle, a sitemap, a robots file, and the metadata for sharing
- [x] The wordmark and the icon. The site uses the real mark.
- [x] A dark screenshot, for the dark version of the page.
- [x] A product list in `src/lib/products.ts`, so a second project under the
      SparkyBin name has somewhere to appear. The site is the browser's for
      now, and it moves under a slug when there is a second one.
- [x] The domain serves the site. <https://sparkybin.com> is the address
      the project is reached at, over HTTPS, and the tree never pointed at
      the GitHub Pages URL in the first place.

### P3 — Work on the browser itself

- [ ] **The sidebar.** Phase one is built. The sidebar shows either the
      bookmarks or the open tabs, and a switch at the top of it says
      which. The bookmarks are in sections, where a section is either the
      bookmarks bar or a tag, and the sections are the user's to choose and
      order in a preferences page. The design note is in
      `docs/sidebar.md`, which also has what is still open: drag and drop
      for the sections, whether the bar stays a section, whether the
      sidebar should be open by default, and whether the tab list should
      become the tab strip rather than a second view of the same tabs.
- [x] **Performance and stability.** A shortlist and a way to measure are in
      `docs/performance.md`, and `tools/measure.sh` collects the numbers.
      The first item was a stability fix rather than a performance one: an
      assertion in the renderer that would take every tab in a web process
      down with it. The second, third and fourth turned out to be bigger:
      the extension API was never delivered, the extension's own files were
      served without a type, and a tab opened in the first second of a
      window was never reported. All three are fixed and all three are
      broken upstream too.
- [x] The last item on the list: an object is unreffed at shutdown that is
      not an object. It was not the table of background views, as the note
      guessed. The bookmarks manager was freed twice, once by the sidebar
      and once by the preferences page, neither of which owned it. The
      browser died on the way out every time. It exits cleanly now.
- [ ] WebExtension gaps, in rough order of how much they matter. The
      missing calls and the events that are never sent are listed in
      `docs/extensions.md`, and the ones thirteen real extensions asked
      for are in. What is left:
      `storage.onChanged` is missing and `storage` writes block the UI
      thread; Manifest V3 is unsupported, and its service workers are why
      three of the extensions serve no files at all;
      `emit_in_tab_with_reply()` can hang on a destroyed view.
      **`browser.webRequest` is the one that cannot be fixed here.** It is
      how an ad blocker says no to a request, and WebKit does not let
      anything cancel a single request. The browser's own filtering does
      work, and is separate.

      But there is another way, and it is the one Safari uses. A content
      blocker stops requests by rule rather than by decision, and WebKit
      has that. The browser now compiles the filter lists that come with
      an extension into the rules a content blocker takes, and puts them
      on the pages, so the list an ad blocker ships with is used. The
      whole of uBlock Origin's list goes through it and comes out as a
      list WebKit takes. `docs/requests.md` has the detail.
- [x] **Requests can be stopped, by rule.** The filter lists that come
      with an extension are compiled into the rules that WebKit's content
      blockers understand, and put on the pages. This is how Safari's
      uBlock Origin works, and it is the only way an extension's rules
      can be used, because WebKit does not let an extension cancel a
      request. `docs/requests.md` has the detail.
- [ ] `browser.declarativeNetRequest`. The translation for it is written
      and tested, but the API is not there yet.
- [ ] `browser.webRequest` for watching and rewriting. The rules for it
      are not written yet. Rewriting is possible: a request can be
      pointed somewhere else.
- [x] **Colours and a browser that a theme cannot break.** The browser had
      no styles of its own at all, so a theme was free to decide that a
      button is twenty pixels tall or that a header bar has no height, and
      the browser then looked broken in ways that had nothing to do with the
      theme being wrong. The browser's own sizes are set now, after the
      theme. There are also fifteen palettes to pick from, named after the
      projects they come from, in the preferences page next to the sidebar.
- [ ] The menus command is carried in an action shared by every extension
- [ ] The `TODO` file: `g_clear_object` in dispose, named timeout sources,
      `g_autofree` where it fits
- [ ] Tests. The `TODO` file is blunt about it: we practically do not test
      any part of this, and that should change. Four of the GUI tests fail in
      a container for environmental reasons, so that needs a baseline to
      compare against.

### P4 — Housekeeping

- [x] Reword the eight verbose commits.
- [x] **Remove the certificate watcher from the site repository.**
      `SparkyBin-site/tools/wait-for-https.sh` existed only until GitHub
      issued the certificate for sparkybin.com and the HTTPS redirect was
      turned on. That has happened, and the script is gone. The site is
      served over HTTPS at <https://sparkybin.com>.
- [ ] A pull request template and an issue template
- [x] `NEWS` mentions that the fork's changes are recorded in `NOTICE`
- [x] `data/generate-release-notes` handles the fork's version format

---

## 7. Order of work

Suggested, roughly in dependency order:

1. Finish P0. Small, and it is the difference between a project that works
   and one that appears to work.
2. Version, manifest renames, and the release workflow. Everything on the
   website depends on there being a release to link to.
3. The website, once there is something real to download.
4. The README rewrite, alongside the website so the voice is consistent.
5. P3, as time allows. The WebExtension work is the fork's real
   contribution and deserves the attention.
