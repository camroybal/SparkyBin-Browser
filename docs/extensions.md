# Web extensions

The browser supports Manifest V2 extensions. This is what was found when a
set of well known extensions was installed and run, and what is still
missing.

## The test

The extensions come from addons.mozilla.org, which is where the Firefox
versions live, and they are unpacked into the profile's `web_extensions`
directory, because that is what the browser reads. Each one is run on its
own with a fresh profile, and the log is read afterwards to see how far it
got.

The things worth counting are:

- **activated** — the extension was found in the profile and switched on
- **served** — how many of the extension's own files the browser fetched.
  An extension with a background page has to fetch it, so this says
  whether the extension started at all
- **api** — how many calls the extension made into the browser's API
- **jserr** — how many errors the extension's own JavaScript hit
- **crit** — how many criticals the browser hit. Anything above zero is a
  bug in the browser

The results before the fixes in this release were that four of the
extensions took the whole browser down, and that the browser hit a critical
of its own on the way out every time, with or without an extension
installed.

## The results

| Extension | Version | Activated | Served | API | JS errors |
|---|---|---|---|---|---|
| uBlock Origin | MV2 | yes | 93 | 0 | 1 |
| Firefox Multi-Account Containers | MV2 | yes | 13 | 27 | 4 |
| ClearURLs | MV2 | yes | 15 | 4 | 2 |
| Dark Reader | MV2 | yes | 8 | 29 | 3 |
| Consent-O-Matic | MV3 | yes | 8 | 7 | 2 |
| Cookie AutoDelete | MV2 | yes | 7 | 6 | 3 |
| Proton Pass | MV3 | yes | 2 | 3 | 4 |
| SponsorBlock | MV2 | yes | 2 | 3 | 2 |
| Decentraleyes | MV3 | yes | 2 | 0 | 1 |
| Violentmonkey | MV2 | yes | 2 | 5 | 3 |
| uBlock Origin Lite | MV3 | yes | 1 | 0 | 0 |
| Stylus | MV2 | yes | 0 | 0 | 0 |
| Tabby | MV3 | yes | 0 | 0 | 0 |

Every extension activates. Nothing takes the browser down, and the browser
hits no criticals of its own. The ones that serve no files are the ones
with a service worker rather than a background page, which is a Manifest V3
thing the browser does not do.

## What was wrong, and what was done about it

### The browser died on any critical

The browser makes GLib criticals fatal, so a critical anywhere takes the
whole browser with it. Four extensions hit one.

### The JSON quoting never worked

`ephy_json_quote_string()` gave a value to a JSON builder that had no
container in it, so json-glib said so with a critical, and the browser
died. Six places call it, including the handlers for commands,
notifications and menus, so any extension that used one of them and then
did anything else took the browser down.

Dark Reader and Firefox Multi-Account Containers both died on the first
call.

### Extensions had no id

The polyfill that most extensions load refuses to run at all without
`browser.runtime.id`, and says so with "This script should only be loaded
in a browser extension." The id of an extension is its GUID, and the
renderer had it, so it is set now.

### Extensions could not open their own pages

The browser's own schemes were not addresses as far as the address check
was concerned, because they are not in the desktop file database. An
extension that opened its own options page with `tabs.create()` hit an
assertion and died.

Consent-O-Matic does exactly that.

### The browser freed the bookmarks manager twice

The manager belongs to the shell, and getting it hands over a pointer that
is not ours to free. Two places took it and then freed it, and the shell
freed it again and found the memory gone. The browser died on the way
out, every time, with or without an extension installed.

### An extension's own JavaScript was not there yet

The background page of an extension is loaded by the browser, and its
JavaScript is not there until it has been read and run. An event that
arrived in between found an empty page, and the extension never heard
about it.

Events now wait for the page to finish loading before they are sent.

## What was put in

- **`browser.permissions.contains` and `browser.permissions.getAll`.** A
  question about the manifest, so it can be answered. Proton Pass asked
  for one and was told nothing.
- **`browser.runtime.onStartup`.** Sent when the background page has
  loaded, which is the browser starting up as far as the extension is
  concerned.
- **`browser.runtime.onInstalled`.** The browser has no install step of its
  own, so the list of the extensions that have been in the profile is
  kept, and one that is not on it is a new one and is told so.
- **`browser.browserAction.setTitle` and `setIcon`.** The title and the
  icon of the extension's button. The icon is either a path inside the
  extension or a data URL.
- **`tabs.onUpdated` and `tabs.onActivated`.** The two events that say
  what a tab is doing, which is most of what an extension does. The first
  is sent with the three arguments Firefox uses.
- **`tabs.onRemoved`** now says what happened to the tab, not just which
  tab it was.

## What is still missing

These are the calls the extension JavaScript offers that the browser does
not answer. They are in the order of how much they hurt.

### `browser.webRequest` — not there at all, and not easy to put there

This is the one that matters, and it is the hard one.

An ad blocker is built on `webRequest`: it is how an extension sees a
request on its way out and says no to it. uBlock Origin, ClearURLs and
Proton Pass all want it, and none of them can do anything useful without
it.

**No ad blocker can work in the browser today.** The browser has its own
filtering, which is separate and does work, but an extension that wants
to block an advertisement has no way to say so.

The catch is that this is not a matter of writing a handler. WebKit does
not let anything cancel one request. The `resource-load-started` signal
says that a request is on its way, and hands over the request so it can
be looked at, but there is no way to say no to it: the only thing that
can stop a load is `webkit_web_view_stop_loading()`, which stops
everything in that view.

So what can be had:

- **Watching** requests is possible, and a `webRequest` that only reports
  could be written today.
- **Rewriting** a request is possible, because the request can be pointed
  somewhere else in the callback. That is the redirect half of what an ad
  blocker does.
- **Cancelling** a request is not possible without a change to WebKit
  itself. That is the half uBlock Origin needs most, and without it the
  extension will not work.

This is worth knowing before spending time on it: the answer is not in
this repository, it is in WebKit.

### Events that are defined but never sent

The JavaScript defines thirty events and the browser sends fourteen of
them. The ones that never arrive:

    cookies.onChanged        permissions.onRemoved
    notifications.onClosed   runtime.onConnect
    notifications.onShown    tabs.onAttached
    pageAction.onClicked     tabs.onDetached
    permissions.onAdded      tabs.onHighlighted
                             tabs.onMoved
                             tabs.onReplaced
                             tabs.onZoomChange
                             windows.onFocusChanged

`tabs.onUpdated` and `tabs.onActivated` are the two that matter most:
without them an extension cannot tell what happened to a tab, which is
most of what an extension does. Both are sent now, and `tabs.onUpdated`
is sent with the three arguments Firefox uses: the tab, what changed, and
the tab again.

### Calls with no handler

    downloads.getFileIcon                   pageAction.getTitle
    downloads.pause                         pageAction.hide
    downloads.resume                        pageAction.setIcon
    menus.removeAll                         pageAction.setTitle
    menus.update                            pageAction.show
    runtime.connectNative                   permissions.remove
    runtime.sendNativeMessage               permissions.request
    runtime.setUninstallURL

`permissions.request` cannot be answered: it is a question for the user,
and the browser has no way to ask one. `permissions.remove` cannot either,
because there are no permissions that were granted at runtime to remove.

The rest could be filled in.

### Namespaces that are not there

`browser.scripting` is Manifest V3 and the browser does not do Manifest
V3, so that is not missing so much as not yet.

### Other things that came up

- `browser.browserAction.setTitle` and `setIcon` are now offered.
- `browser.storage.sync.onChanged` is not defined. SponsorBlock wants it.
- An extension that uses a dynamic `import()` fails, because the
  extension's world does not have it. Decentraleyes does.
- `browser.webRequest` is not there, and cannot be put there without a
  change to WebKit. See above.
- `browser.management`, `browser.contextualIdentities` and
  `browser.extension.isAllowedFileSchemeAccess` are not offered.
  Firefox Multi-Account Containers wants all three.

## Running the test again

The extensions are fetched from addons.mozilla.org and unpacked, then each
one is run in a profile of its own with a fresh set of settings. The
browser is started under Xvfb, and the log is read afterwards.

    tools/extension-test.sh

The script needs a built and installed browser, and a display.
