# Requests: blocking, watching and rewriting

How the browser can be made to see requests, what WebKit lets it do, and
what that means for ad blockers.

## What WebKit gives us

WebKitGTK 6.0 (WebKit 2.54) has four places where the browser can see a
request. Only one of them can stop it.

**Watching a request.** `WebKitWebView::resource-load-started` says that a
subresource is on its way, and hands over the `WebKitURIRequest`. Every
image, script and stylesheet goes past it. It is a notification: by the
time it is called, the request is already on its way.

**Redirecting a request.** The request in that signal can be pointed
somewhere else:

```c
webkit_uri_request_set_uri (request, "https://example.com/other");
```

So a request can be sent somewhere else, but not stopped.

**Failing a request.** `webkit_uri_scheme_request_finish_error()` fails a
request, but only for the schemes the browser has registered itself. `http`
and `https` are WebKit's.

**Blocking a request.** This is what a content blocker is:

```c
webkit_user_content_manager_add_filter (ucm, filter);
```

A `WebKitUserContentFilter` is a compiled list of rules in Safari's
content blocker format, and the rules in it stop requests before they
leave the machine. There is no JavaScript in the loop and no round trip
to another process, so it is fast and it cannot be raced.

## Why that is the only way, and why Safari's uBlock Origin works

Safari has uBlock Origin and Safari has no `webRequest` blocking either.
The answer is that Safari's uBlock Origin does not block requests from
JavaScript. The Safari Web Extension converter turns uBlock Origin's
filter lists into a content blocker at build time, and the extension
itself is left with the cosmetic half of the work: hiding the elements
that the rules point at.

Chrome arrived at the same place from the other direction. When Manifest
V3 removed blocking `webRequest`, Chrome added
`browser.declarativeNetRequest`, which is the same idea as a content
blocker: the extension declares rules, the browser enforces them without
asking the extension first.

So the answer to "how do we do ad blocking in a WebKit browser" is the
same answer Safari and Chrome arrived at, and it is the one that WebKit
supports natively.

## What that means here

There are three things to build, and they are worth doing in this order.

### 1. Parse the filter lists ourselves

The browser already blocks advertisements, using lists that a third party
converts for it. That is a dependency on somebody else's server, and it
means the lists cannot be chosen by the user.

The browser should parse the lists itself. The format is the Adblock Plus
one, which is a line per rule and is well documented. The network rules
are the ones that matter here; the cosmetic ones (`##`) are the
extension's business, and the browser does not need them.

This is worth doing on its own. A browser that lets you subscribe to a
filter list, or point it at a file on disk, is more useful than one that
downloads a converted copy of somebody else's list.

It is also what makes an installed ad blocker work. uBlock Origin ships
its lists as plain text in `assets/ublock/`, and the browser can compile
those into rules of its own. The extension then does the cosmetic half,
which it already can.

### 2. `browser.declarativeNetRequest`

The standard API for declarative blocking, and the one that Manifest V3
blockers use. uBlock Origin Lite, AdGuard and others expect it.

The rules are not the same shape as content blocker rules, but they are
close: a URL pattern, a set of resource types, and an action. The
translation is mostly the URL pattern, which is a pattern in one and a
regular expression in the other.

### 3. `browser.webRequest`, for watching and rewriting

Not blocking, because that cannot be done. Watching and rewriting can.

`resource-load-started` gives every request, so `onBeforeRequest` can be
delivered, and the request can be pointed somewhere else, so a rule that
says "send this URL somewhere else" can be honoured.

That is all ClearURLs wants, and it is a privacy tool that people ask
for by name.

## What was built

### The filter compiler

`lib/ephy-filters-compiler.c` turns a list of filters into the rules that
WebKit's content blockers understand. It reads the Adblock Plus format,
which is what the popular ad blockers use and what the browser's own
lists are in.

The URL patterns are the same in both formats, so the same translation does
both the lists and the rules of `browser.declarativeNetRequest`.

A content blocker is not quite a regular expression. It has no alternation,
no repetition and no shorthands for the character classes, and one rule it
cannot take stops the whole list from being used. The compiler knows those
limits and leaves out the rules that break them, rather than handing over
a list that will not compile.

The rules that a filter list says are exceptions come first, because a
content blocker stops at the first rule that matches.

The whole of uBlock Origin's list of filters goes through this and comes
out as a list WebKit takes: 25,792 lines in, 3,216 rules out, the rest
being the rules that hide things in a page, which are the extension's
business.

### The lists that come with an extension

An ad blocker has its lists inside the extension. The browser looks for
them in `assets/ublock/` and `assets/filters/`, compiles them, and puts
the rules on the pages.

That is how Safari's uBlock Origin works, and it is the only way an
extension's rules can be used, because there is no other way for an
extension to stop a request.

The browser's own rules and an extension's rules are the same thing once
they are compiled, so they are put on the page in the same place.

### What was checked

`tests/test-ephy-filters-block.c` starts a web server, compiles a filter
list, puts the rules on a page, and loads a page with an image in it. The
server is asked for the page and not for the image, so the request did
not happen.

`tests/test-ephy-filters-compiler.c` has fifteen checks on the
translation, one for each of the things a filter list can say.

## What was not built

**`browser.declarativeNetRequest`.** The translation for it is written
and tested, but the API is not there yet, so nothing can use it.

**`browser.webRequest` for watching and rewriting.** The rules for it are
not written yet either. `resource-load-started` gives every request and
the request can be pointed somewhere else, so it can be done, but it has
not been.

**The cosmetic half.** A content blocker can hide things with
`css-display-none`, which is what Safari's content blockers use, and the
Adblock Plus cosmetic rules are selectors. The translation for those is
not written.

**Blocking from a `webRequest` listener.** The listener runs in the
extension's process, and the browser's process would have to wait for its
answer before letting the request through. There is no way to do that
without a round trip to another process for every request, which is what
`webRequest` blocking does in Firefox, and which is slow enough that
Firefox deprecated it.

There is no way to make it work in WebKit. It needs a change in WebKit:
something that lets the network layer ask a callback and wait for the
answer.

**The dynamic half of an ad blocker.** Clicking "block this element" in
an ad blocker needs the request to be stopped as it happens, so it is out
of reach for the same reason. A browser can block by list and hide by
script, but it cannot block by decision.

**Manifest V3 service workers.** The browser has no service worker
support, so an MV3 extension has no background script and does nothing.
uBlock Origin Lite is Manifest V3, so it needs this before it needs
anything else. uBlock Origin itself is Manifest V2 and its background page
works.
