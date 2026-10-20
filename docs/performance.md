# Performance and stability

What can be done in the code around WebKit, and what cannot. Written before
any of it is changed, so that the changes can be checked against a
measurement rather than a feeling.

## The honest part first

The browser is a thin shim over the engine. It does not paint, lay out, run
JavaScript, resolve DNS, or speak HTTP. Those are WebKit's, and a change
there is a change to WebKit.

WebKitGTK 6 exposes no way to cap the number of web processes either, so
that is fixed when WebKit is built.

What is left is the app's own work, and it is not nothing:

- what the app does on every page load
- what it does at startup, and how much of that is on the critical path
- how many web views it keeps alive
- what it writes to disk
- how much it does per frame, per request and per extension event

That is the list worth working through.

## Measure first

There is no measurement in this project yet, which is the same problem the
`TODO` file has about testing. Without a baseline, an optimisation is a
change that might be making things better.

Three numbers would do, and `tools/measure.sh` collects them:

**Time to window.** How long from starting the process to a window being on
screen. This is the one users notice on every launch, and it is the one most
affected by the startup work below.

**Resident memory.** How much the browser and its web processes use, with a
given number of tabs open. WebKit gives each site a separate process, so
this grows with the number of sites rather than the number of tabs.

**Web process count.** How many processes are alive. This is the multiplier
on everything else.

```sh
tools/measure.sh build/src/sparkybin-browser https://example.com
```

The numbers to beat are whatever the unmodified tree gives, on the same
machine, with the same cache state. A cache state matters: the first run
after a profile is created does more work than the tenth.

The script is useful, but it is a first cut. Two caveats. It counts the
browser and its descendants, and the web processes are not always direct
children, so the process count can be low. And in a container without a
graphics card, WebKit falls back to software rendering, which uses far more
memory than it would on a real desktop. The numbers from a container are only
comparable with other numbers from a container.

## The shortlist

In rough order of how much is likely to be worth it.

### 1. The renderer asserts, and an assert there kills every tab

`embed/web-process-extension/ephy-web-process-extension.c`:

```c
js_browser = jsc_context_get_value (js_context, "browser");
g_assert (!jsc_value_is_object (js_browser));
```

The renderer turns fatal criticals into web process aborts, so if this ever
fails it takes the whole web process with it, and every tab in it. The
assertion is there to catch a double installation of the extension API, but
`window-object-cleared` is emitted once per document, so it holds today by
luck rather than by design.

**This one is done.** Both of the places that had it now warn and return
instead, so a double installation costs a warning rather than the process.

### 2. The extension API was never delivered

This one is done, and it was the largest of the three.

`embed/web-process-extension/resources/js/webextensions-common.js` defined the
event listeners with a private method:

```js
#emit (...data) { ... }
```

but the UI process reaches them by evaluating a script it builds, and a
private method cannot be reached that way:

```c
script = g_strdup_printf ("window.browser.%s._emit(%s);", name, json);
```

So every event threw, and `_emit_with_reply`, which is how
`runtime.onMessage` asks the extension views for an answer, never worked
either. The methods are public now, and named the way the browser process
calls them.

### 3. Extension files were served without a type

Also done.

The `ephy-webextension://` scheme handler finished the response without
saying what it was:

```c
webkit_uri_scheme_request_finish (request, stream, length, NULL);
```

A background page therefore came back as text rather than as a document, and
the scripts in it were never fetched. The type now comes from the name of the
file.

### 4. Tabs opened in the first second were never reported

Also done.

The tab signals were connected in a timeout callback, a second after the
window was added, because the delay was there for the window event, which
needs the window to have settled before it can be described. A tab opened in
that window in that second was never reported to the extension. The signals
are connected straight away now, and the delay is left to do only what it was
for.

With all three fixed, an extension's background script runs, its events reach
it, and the storage it writes is written. The extension that finds this out
is the one that was working in someone's Firefox.

### 5. The extension API is installed per frame, per extension

The renderer installs the extension API into every frame of every extension
page, and checks the frame's URI to decide whether it belongs to the
extension. That check runs for every frame of every extension, on every
navigation. With several extensions and a page with iframes, it adds up.

Whether it can be hoisted out of the per-frame path is worth a look, but the
security model depends on the check being per frame, so this one is
constrained rather than merely difficult.

### 6. The cache model is never set

`webkit_web_context_set_cache_model()` exists and the browser never calls
it, so it gets WebKit's default. The default is right for a browser, but
"right by default" is not the same as "chosen", and the difference is a
cache sized for a single page rather than for a browsing session. The
setting is one line, and it is measurable in resident memory.

### 7. The extension scan is on the startup path

`EphyWebExtensionManager` is a process-wide singleton, and its constructor
calls `ephy_web_extension_manager_scan_directory_async()`. The scan is
therefore triggered by whatever asks for the manager first, which is usually
something during the first window's construction, so it lands on the startup
path.

Two costs: the directory is read while the window is being built, and no
extension is known until the scan finishes, so a page that loads immediately
does not have its extension API yet.

Making the scan happen earlier, off the window's construction path, would
take it off the critical path. The singleton makes this easy to get wrong
in the other direction though, so it needs care.

### 8. Autofill and the password manager run on every page

The autofill machinery connects to the page load and asks the password
manager for the site's credentials. The password manager is backed by
libsecret, and a keyring query is not free, and it happens on every
navigation whether or not the user has ever saved a password for the site.

The obvious improvement is to skip the work entirely when there are no
passwords stored, which is a check on a count rather than a keyring query.

### 9. Website data storage is one switch for three things

`webkit_pref_callback_enable_website_data_storage()` sets the HTML5
database, local storage and the cookie policy from a single setting. Turning
the setting off turns off cookies, which is not what someone who wants less
disk churn expects.

Splitting the storage settings from the cookie setting is a small change
that makes the setting mean what it says. The HTML5 database is deprecated
and no current site uses it, so it is a good candidate for being off.

### 10. The bookmarks sidebar rebuilds its whole list

`update_bookmarks_order()` clears the bookmarks order and re-adds every row
when the list changes, and then saves. It runs on several different
triggers. For a few hundred bookmarks that is fine, and for a few thousand
it is not. Worth a look once the sidebar is rebuilt, since the sidebar work
touches the same code.

### 11. The tab overview keeps a live view per tab

`AdwTabOverview` shows a thumbnail per tab, and each one is a live web view.
With a lot of tabs open that is a lot of live views, which is the largest
single contributor to the memory number. Adwaita already handles this
better than most, and the fix is not ours to make, but it is worth knowing
which knob turns it.

### 12. An object is unreffed at shutdown that is not an object

This one is not fixed, and it is only a warning, but it should be looked at.

When the browser exits with an extension loaded, the manager's dispose
destroys the table of background views:

```c
self->background_web_views = g_hash_table_new_full (g_direct_hash, g_direct_equal,
                                                    NULL, (GDestroyNotify)g_object_unref);
```

and that gives:

```
GLib-GObject-CRITICAL: g_object_unref: assertion 'G_IS_OBJECT (object)' failed
```

The view is created with `g_object_new()`, which for a widget leaves a
floating reference, and the table takes a pointer to it without taking a
reference. Either the floating reference is never sunk, or the view has been
finalised by the time the table is destroyed, and what the table is left
holding is no longer a widget.

The fix is to be explicit about who owns the view: sink the reference when
it is created, and let the table destroy it.

## What is not worth doing

**Micro-optimising the app.** The app is not where the time goes. A
micro-optimisation in the app is invisible next to a 200 millisecond page
load, and it is the kind of change that is hard to measure and easy to get
wrong.

**Chasing the engine.** Anything about paint, layout, compositor frames or
the network stack is a WebKit change. It is worth filing, and it is worth
being careful about carrying patches, but it is not this project's work.

**Guessing.** Every item above is a hypothesis until it is measured. Some
of them will turn out to be noise. That is fine, as long as the measurement
says so and the change is reverted.

## A note on the tests

Four of the GUI tests fail in a container for environmental reasons, not
because of anything in the tree. Any performance work needs the same kind
of baseline: run it against the unmodified tree, on the same machine, and
compare.
