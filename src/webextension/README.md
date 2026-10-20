# SparkyBin Browser WebExtensions

This is an implementation of [WebExtensions](https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API).

Some examples to run can be found here: https://github.com/mdn/webextensions-examples

Upstream describes this as a work in progress that should be assumed insecure,
incomplete, and full of issues. **This fork enables it by default** (see
[Enabling and activation](#enabling-and-activation)), so it is worth being
explicit that only the default changed: the API surface is still partial and
the security posture is unchanged. Read
[Security model](#security-model) and [Known limitations](#known-limitations)
before adding to it.

Upstream tracks issues here:
https://gitlab.gnome.org/GNOME/epiphany/-/issues?label_name[]=5.+WebExtensions

This fork's issues are at https://github.com/camroybal/SparkyBin-Browser/issues

## Feature Set

- Un/Load/Enable/Disable xpi and extracted extensions
- Works for existing and new views
- Manifest file:
    - initial content_scripts
    - initial background page
    - initial background scripts

### WebExtension JavaScript APIs

All of these APIs follow Firefox in behavior. They have the entrypoint of `browser` and return a `Promise`.

However just like Firefox we try to retain compatibility with Chrome and the `chrome` entry point works and if the last argument is a function it is treated as a callback with `lastError` set appropriately.

#### alarms

The alarms API is fully implemented.

- clear()
- clearAll()
- create()
- get()
- getAll()
- onAlarm

#### browserAction

- onClicked

#### cookies

Limitations:

- Epiphany only has a single cookieStore and all APIs will always use it.
- Filtering by firstParty domains isn't supported.
- You cannot request all cookies, a domain must be given.
- onChanged is only a stub and will never be called.

APIs:

- get()
- getAll()
- getAllCookieStores()
- set()
- remove()
- onChanged

#### downloads

Limitations:

- Unlike other browsers downloadIds are not persistent across sessions. Downloads are always "incognito" as they are always lost.
- Pausing/Resuming downloads is not supported.

APIs:

- cancel()
- download()
- erase()
- open()
- removeFile()
- show()
- showDefaultFolder()
- search()
- onCreated
- onChanged
- onErased

#### extension

- getViews()
- getBackgroundPage()
- getURL()

#### i18n

This is only partially implemented, see https://gitlab.gnome.org/GNOME/epiphany/-/issues/1791

- getMessage()
- getUILanguage()

#### runtime

This is partially implemented. Message passing including replies works including from Content Scripts.

- getBackgroundPage()
- getBrowserInfo()
- getPlatformInfo()
- getURL()
- openOptionsPage()
- sendMessage()
- onMessage
- lastError

#### menus

Notes:

- `contextMenus` is aliased to this API and the same permissions work for both.

Limitations:

- Menus are only shown on web pages and not extension views, tabs, etc. (TODO)
- Radio and Checkbox menus aren't properly supported yet. (TODO)
- Commands are not yet supported. (TODO)
- update() isn't supported yet. (TODO)
- Frames are not properly handled.
- Icons are not supported.

APIs:

- create()
- remove()
- removeAll()
- onClicked

#### notifications

Limitations:

- Icons are not supported.
- We can't track when notifications are shown or dismissed.

APIs:

- create()
- clear()
- getAll()
- update()
- onClicked
- onButtonClicked

#### pageAction

- setIcon()
- setTitle()
- getTitle()
- show()
- onClicked

#### storage

Limitations:

- Sync storage isn't implemented but possible as Epiphany uses Firefox Sync.
- onChanged isn't implemented (TODO)

APIs:

- local.set()
- local.get()
- local.remove()
- local.clear()

#### tabs

Limitations:

- Some of these APIs are supposed to return after the page is loaded but return immediately. (TODO)
- Only some of the events are sent. `onCreated` and `onRemoved` are, with the
  tab object. The others are not, and the event objects exist so that an
  extension can register a listener for them, but nothing ever calls it.

APIs:

- create()
- query()
- insertCSS()
- remove()
- removeCSS()
- get()
- getZoom()
- setZoom()
- update()
- reload()
- executeScript()
- sendMessage()

#### windows

- get()
- getCurrent()
- getLastFocused()
- getAll()
- create()
- remove()
- onCreated
- onRemoved

### Tested extensions

- apply-css
- borderify
- notify-link-clicks-i18n

## Runtime behaviour

### Enabling and activation

Two independent things have to be true for an extension to do anything.

**The feature is enabled.** The `enable-webextensions` key of the
`com.sparkybin.Browser.web` schema defaults to `true` in this fork (upstream
defaults it to `false`). It controls whether the Extensions page is offered in
the Preferences; the manager itself is created for every window from
`ephy_window_constructed()`.

**The individual extension is active.** That is the `webextensions-active`
string list, and it is matched against the extension's **name** taken from its
manifest. Two consequences follow, and both are real:

- Two extensions that declare the same `name` are enabled and disabled
  together, because nothing distinguishes them in the setting.
- Renaming an extension in its manifest silently drops it from the list, so it
  comes back disabled.

The GUID would be a better key, but it is generated with
`g_uuid_string_random()` on every load, so it is not stable across restarts.
Keying on it would disable every extension on the next launch. A stable
identifier would have to come from the manifest, which Manifest V2 does not
reliably provide.

Extensions are read from `<profile>/web_extensions/`, where `<profile>` is
`g_get_user_data_dir()/com.sparkybin.Browser` — normally
`~/.local/share/com.sparkybin.Browser/web_extensions/`. Each extension is
either an extracted directory or an `.xpi`. Extension local storage is written
to `g_get_user_config_dir()/com.sparkybin.Browser/web_extensions/<directory>/local-storage.json`.

### The WebKit WebExtension object is optional

`ephy_web_extension_parse_manifest()` calls `webkit_web_extension_new()`, but
**that function can return `NULL` without setting a `GError`** — observed with
WebKitGTK 2.54.1 for directories, archives, Manifest V2 and Manifest V3 alike.
Epiphany therefore must not treat it as always present.

This used to be a silent and total failure, and it is worth knowing why,
because the symptoms are confusing:

- `webkit_web_extension_get_display_name()` returned `NULL`, so
  `ephy_web_extension_get_name()` was the empty string. Since the name is the
  activation key, the extension could never be activated.
- `ephy_web_extension_has_background_web_view()` asked WebKit whether there was
  background content. With a `NULL` object that assertion failed and returned
  `FALSE`, so the background page was never loaded even though Epiphany had
  already parsed `background_page` correctly.

So: the name, version and description are parsed from the manifest by Epiphany
itself, and the WebKit object is only a fallback. If you add an accessor, do not
assume the object exists — check for `NULL`, and prefer Epiphany's own manifest
parsing for anything the extension cannot work without.

### Security model

The isolation that this implementation actually depends on, all of which has to
keep working:

- Extension views may only navigate within `ephy-webextension://<their own
  guid>/`. This is enforced in the UI process by `decide_policy_cb()`.
- The API is installed into a frame only if the frame's own URI belongs to that
  extension, not merely if the page's URI does. WebKit emits
  `window-object-cleared` for every frame, so an extension page that embeds a
  foreign frame would otherwise hand that frame the extension's identity.
- Content scripts run in a private `WebKitScriptWorld` per extension. Page
  JavaScript has no path to `browser`.
- Extension pages run under the extension's own Content Security Policy.
- The files of an extension are served by the `ephy-webextension://` scheme
  handler in the UI process. The response has to say what type it is, or a
  background page comes back as text rather than as a document and the scripts
  in it are never fetched. The type comes from the name of the file.

### Debugging

`G_MESSAGES_DEBUG=all` is **not** enough to see this code's own `g_debug()`
output: `ephy_debug_init()` installs a handler for the application's log domain
that drops it. Set `EPHY_LOG_MODULES=all` as well (or list message substrings
colon-separated). Debug output from libraries such as libsoup and GTK does
appear with `G_MESSAGES_DEBUG` alone, which makes the omission easy to
misdiagnose.

Console output from an extension's own pages is forwarded to stdout, because
extension web views are created with `enable-write-console-messages-to-stdout`.

## Overview

### Contexts

Extensions generally have content running in their own private pages. That is pages under the `ephy-webextension:` URL scheme.
These pages have full access to the WebExtension API available in the default JavaScriptWorld.

Extensions can also inject [Content Scripts](https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/Content_scripts)
which run in a private JavaScriptWorld per-extension on each page it has permission to.

## How to add API

Adding a new API involves touching a few areas.

- `embed/web-process-extension/resources/js/webextensions-common.js` and `embed/web-process-extension/resources/js/webextensions.js` These files contain JavaScript injected into pages. The former is injected for Content Scripts and Extension Pages while the latter is only injected into Extension Pages. They do little more than expose the API.

- `embed/web-process-extension/ephy-web-process-extension.c` This is the WebKitWebProcessExtension. It installs the API into Extension Pages and sets up the
private JavaScriptWorlds used for Content Scripts.

- `embed/web-process-extension/ephy-webextension-common.c` This is shared by the API installation paths in
`ephy-web-process-extension.c`. It is the bulk of the `extension`/`runtime` and `i18n` APIs. It also handles much of the message passing to the UI process described below.

- `src/webextension/ephy-web-extension-manager.c` This is where all extensions are managed in the UI process and also where all messages from web views are
handled; In `content_scripts_handle_user_message()` and `extension_view_handle_user_message()` specifically with `api_handlers` being defined at the top of the file. These call out to the real API handlers.
This is also where a lot of event handling happens. The manager will listen to signals throughout Epiphany and call `ephy_web_extension_manager_emit_in_extension_views()`
to trigger events.

The event listeners are objects of the class `EphyEventListener` in
`embed/web-process-extension/resources/js/webextensions-common.js`. The manager
reaches them by evaluating a script it builds, so the method it calls has to
be public and has to be called `_emit`: the name is in the string the manager
formats, and a private `#emit` cannot be reached that way. The same applies to
`_emit_with_reply`, which is how `runtime.onMessage` asks the extension views
for an answer.

- `src/webextension/api/*` This is where all actual API handlers are implemented. A handler is given the source of the call (the WebExtension, its WebKitWebView), the name
of the call, the arguments (which were serialized over JSON), and a GTask to respond to the call.

NOTE: Please use `menus.c`/`menus.h` as an example starting point. The handlers have evolved to be written in a few styles with this being the direction we want to go in. It uses
only asynchronous API handlers and uses `json-glib` for all JSON parsing.

Be very careful about parsing the JSON arguments to be defensive against invalid values. The funcitons in `ephy-json-utils.h` help you by being strict on expected types.

Every API handler must return either an error with `g_task_return_new_error (task, WEB_EXTENSION_ERROR, ..., "foo.function(): Helpful reason");` or return a valid JSON
string with `g_task_return_pointer (task, allocated_json_string, g_free);`. `JsonBuilder` is helpful for building complex response objects or a simple `g_strdup ("true");`
works for some APIs. Many APIs require an empty response on success which is just `g_task_return_pointer (task, NULL, NULL);`. Your handler is entirely asynchronous and you
can store the GTask for later.

Any string that comes from an extension and is embedded in a JSON response must
go through `ephy_json_quote_string()`. Interpolating it with `g_strdup_printf
("\"%s\"", value)` lets a quote or a backslash corrupt the response.

## Known limitations

Beyond the per-API gaps listed above, these are the structural limitations to be
aware of before building on this code.

- **Activation is keyed by extension name**, with the consequences described
  under [Enabling and activation](#enabling-and-activation).
- **Manifest V2 only.** Manifest V3 `background.service_worker`,
  `host_permissions` and the object form of `web_accessible_resources` are not
  supported; the object form is skipped silently.
- **`storage` writes are synchronous on the UI thread** and `storage.onChanged`
  is not implemented (`api/storage.c`).
- **`emit_in_tab_with_reply()` has a known race**: a view that is destroyed
  before it responds leaves the message unresolved (see the `FIXME` in
  `ephy_web_extension_manager_emit_in_tab_with_reply()`).
- **The renderer asserts that `browser` is not already defined** in
  `window-object-cleared`. WebKit emits that once per document, so this holds
  today, but it would abort the web process if that ever changed.
- **A `NULL` WebKitWebExtension is expected**, not exceptional — see
  [The WebKit WebExtension object is optional](#the-webkit-webextension-object-is-optional).
- `ephy_web_extension_manager_scan_directory_async()` runs when the manager is
  first created, which happens per window. Extensions are therefore not known
  before the first window exists.

### Still open from the last audit

These were found by reading and by running the browser with a real extension.
They are known, not yet fixed:

- `menus` command items were wired through the shared
  `webextension-context-menu` shell action by storing the command as object data
  and connecting a handler per item per right-click. That is now done through
  the action's target variant, so every item dispatches correctly, but the
  command is still carried in a singleton action shared by all extensions.
- `json_array_add_element()` is transfer-full (verified against json-glib), so
  the handlers that do not unref after adding are correct. Do not "fix" them.
- `ephy_web_extension_manager_set_active()` is guarded against being called
  twice for the same extension, but the guard is object data on the extension;
  it is not a general invariant.
