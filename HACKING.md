Here are some tips to ease your introduction to SparkyBin Browser development.

# Building the Code

SparkyBin Browser often requires an unstable development release of WebKit to build
successfully. The combination of dependencies that is known to work is the one
pinned in `com.sparkybin.Browser.Devel.json`, which is also the manifest GNOME
Builder uses (see the README).

## Building in a container

There is no need to install anything on the host. This is one recipe that works,
using a throwaway Fedora container. It is how this fork has been built and
tested, so the dependency list below is known to be complete:

```
$ podman run --rm -v "$PWD:/src:Z" -v "$PWD/build:/build:Z" \
    --entrypoint bash fedora:latest -c '
      dnf -y install meson ninja-build gcc pkgconf-pkg-config git gettext-devel \
        glib2-devel gtk4-devel libadwaita-devel json-glib-devel libsoup3-devel \
        webkitgtk6.0-devel sqlite-devel libarchive-devel libpwquality-devel \
        libportal-devel libportal-gtk4-devel gcr-devel gdk-pixbuf2-devel \
        libxml2-devel nettle-devel gmp-devel iso-codes-devel libsecret-devel \
        cairo-devel gstreamer1-devel gsettings-desktop-schemas-devel \
        desktop-file-utils python3-gobject python3-docutils itstool \
        blueprint-compiler appstream
      git config --global --add safe.directory "*"
      meson setup /build --buildtype=debugoptimized && ninja -C /build'
```

The tree compiles with no warnings, so anything the compiler reports is worth
looking at. Note that `-Werror=declaration-after-statement` is enabled: all
declarations must be at the top of a block.

## Running the tests

The tests are GUI tests, so they need a display, a session bus and an
`XDG_RUNTIME_DIR`. The build sets `GSETTINGS_BACKEND=memory` and the schema path
for you. Inside the container:

```
$ podman run --rm -v "$PWD:/src:Z" -v "$PWD/build:/build:Z" \
    --entrypoint bash fedora:latest -c '
      dnf -y install xorg-x11-server-Xvfb xorg-x11-xauth glibc-langpack-en \
        dbus-daemon procps-ng
      rm -f /etc/machine-id
      dbus-uuidgen --ensure=/etc/machine-id
      export XDG_RUNTIME_DIR=/tmp/rt
      mkdir -p $XDG_RUNTIME_DIR && chmod 700 $XDG_RUNTIME_DIR
      export LANG=C.UTF-8 LC_ALL=C.UTF-8 GDK_BACKEND=x11
      cd /build && dbus-run-session -- xvfb-run -a meson test --no-suite environmental'
```

Four tests — File helpers, Shell, Embed shell and Snapshot service — are in the
`environmental` suite because they need a working bubblewrap/FUSE document
portal and a writable `/root`. They fail in a container, so the command above
skips that suite. A plain `meson test` still runs it, which is useful on a
desktop; run it deliberately with `meson test --suite environmental`.

## Testing an extension by hand

Extensions are read from `<profile>/web_extensions/`, where `<profile>` is
`g_get_user_data_dir()/com.sparkybin.Browser`. To try one without touching your
real profile, point `XDG_DATA_HOME` and `XDG_CONFIG_HOME` at a scratch directory
and seed the settings with the keyfile backend, which needs no D-Bus:

```
$ P=/tmp/scratch
$ mkdir -p $P/data/com.sparkybin.Browser/web_extensions/demo $P/glib-2.0/settings
$ cp -r my-extension/* $P/data/com.sparkybin.Browser/web_extensions/demo/
$ cat > $P/glib-2.0/settings/keyfile <<EOF
[com/sparkybin/browser/web]
enable-webextensions=true
webextensions-active=['Name From The Manifest']
EOF
$ HOME=$P XDG_DATA_HOME=$P/data XDG_CONFIG_HOME=$P \
    GSETTINGS_BACKEND=keyfile GSETTINGS_SCHEMA_DIR=build/data \
    EPHY_LOG_MODULES=all build/src/sparkybin-browser --new-window
```

The name in `webextensions-active` must match the extension's manifest `name`
exactly; activation is keyed by name. Console output from the extension's own
pages is forwarded to stdout, which is the easiest way to tell whether its
background page ran. Its storage is written to
`<config>/web_extensions/<directory>/local-storage.json`.

See `src/webextension/README.md` for how the implementation fits together.

# Other Documents

- `docs/sidebar.md` — a design note for the sidebar, and what it collides
  with
- `docs/performance.md` — what can be done about speed and stability around
  WebKit, and how to measure it
- `src/webextension/README.md` — how the extension implementation works
- `PLAN.md` — what is planned, and what is still open

# Measuring

`tools/measure.sh` reports how long the browser takes to put a window on the
screen, how many processes it ends up with, and how much memory they use:

```
$ tools/measure.sh build/src/sparkybin-browser https://example.com
```

It needs `xdotool` for the startup time, and it uses a scratch profile so
the numbers do not depend on the real one. See `docs/performance.md` for
what the numbers mean and what they are good for.

# Code Style

In order to keep the code nice and clean we have a few requirements you'll
need to stick to in order to get your patch accepted:

 * Use 2-space no-tabs for indentation.

 * Use K&R style for the braces.

 * No braces for one line control clauses except when another clause in the
   chain contains more than one line.

 * Callback functions have a suffix _cb.

 * All files have to be encoded in UTF-8.

 * Use `char`/`int`/`double`/…, not `gchar`/`gint`/`gdouble`/… types.

 * All implementation files must include first `"config.h"`, followed by
   the primary header, followed by a blank line, followed by all the
   local headers sorted alphabetically, followed by a blank line,
   followed by all the system headers sorted alphabetically. Headers
   should follow the same pattern excluding the config.h and
   self file section, for obvious reasons.

 * Make comments full sentences. This means proper capitalization and
   punctuation.

 * `data/kr-gnome-indent.cfg` is provided as a reference config file for the
   uncrustify program to get correct indentation in new files.

 * There's no space between a type cast and the variable name:  Right:
   `(int *)foo`. Wrong: `(int*) foo`.

 * Avoid explicit comparisons against TRUE, FALSE, and NULL. Right:
   `if (!condition)`, `if (!pointer)`, `if (integer == 0)`. Wrong:
   `if (condition == FALSE)`, `if (pointer == NULL)`, `if (!integer)`.

 * Use `g_signal_emit ()` if a signal is part of the class. Otherwise, use
   `g_signal_emit_by_name ()`.

# Code Structure

## Layering

The code is currently structured into layers, where higher-level layers have
full access to lower-level layers, but lower-level layers have no access to the
higher layers except via delegate objects. (See `EphyEmbedContainer` for an
example of a delegate interface that allows `embed/` limited access to
`EphyWindow`, even though `EphyWindow` is in `src/`.) The levels are:

 * `src/`, the highest layer, mostly GUI stuff
 * `embed/`, stuff relating to the web view
 * `lib/` lowest layer, helper classes that don't need higher-level stuff

The build system enforces that higher-level layers are not in the include path
of lower-level layers, so you should not be able to break the layering unless
you go out of your way to do so.

## GtkApplication and EphyShell

SparkyBin Browser has one singleton `EphyShell` object. Its inheritance hierarchy is:

```
 - GApplication
 --- GtkApplication
 ----- EphyEmbedShell
 ------- EphyShell
```

There is exactly one instance of `EphyShell`, and it is also both the
`EphyEmbedShell` and the `GtkApplication`. Use normal GObject casts to get a
pointer to the type you need.

`EphyShell` is a singleton object where we put all our global state, so it's
kind of like having a global variable, but more organized. You can access it
from anywhere in `src/` using `ephy_shell_get_default()`.

`EphyEmbedShell` is a separate class from `EphyShell` for layering purposes. It
is accessible anywhere from `embed/` or `src/`. So if you have global stuff
that you need to access from `embed/`, you need to put it in `EphyEmbedShell`,
not `EphyShell`.

## Important SparkyBin Browser Objects

`EphyWindow` is a subclass of `GtkApplicationWindow`, which is a subclass of
`GtkWindow`. It's the window. You can have any number of windows open at a time.
`EphyWindow` contains (a) an `EphyHeaderBar` (subclass of `GtkHeaderBar`), and
(b) an `EphyTabView` (contains a `HdyTabView`). `EphyTabView` contains one
or more tabs, and each tab is an `EphyEmbed`. That's worth repeating: an
`EphyEmbed` corresponds to one browser tab. Each `EphyEmbed` contains an
`EphyWebView` (subclass of `WebKitWebView`). This is the object that actually
displays the web page, where all the web browser magic happens.

## Important WebKitGTK Objects

WebKitGTK is a WebKit port that provides a GTK API wrapper around WebKit.

WebKit is really nice. It encapsulates 95% of the complexity of building a web
browser, like the JavaScript engine, HTML layout engine, and actually rendering
the webpage. SparkyBin Browser only has to deal with the remaining 5%. The most important
WebKitGTK objects are:

 * `WebKitWebView` (superclass of `EphyWebView`). Displays the web.
 * `WebKitWebContext`, a global object that manages shared state among web views.

SparkyBin Browser has one `EphyWebView` per browser tab. It has exactly one
`WebKitWebContext`, stored by `EphyEmbedShell`. WARNING: you need to be careful
to use the web context from `EphyEmbedShell` when using a WebKit API that
expects a `WebKitWebContext`. Do not use WebKit's default `WebKitWebContext`;
that is, do not pass `NULL` to any `WebKitWebContext *` parameter, and do not
use `webkit_web_context_get_default()`.

There is separate documentation for the [main WebKitGTK API](https://webkitgtk.org/reference/webkit2gtk/unstable/index.html),
for the [WebKitGTK DOM API](https://webkitgtk.org/reference/webkitdomgtk/unstable/index.html),
and for the [WebKitGTK JavaScriptCore API](https://webkitgtk.org/reference/jsc-glib/unstable/index.html).

## Modern WebKit Process Architecture

Modern WebKit (formerly WebKit2) has a multiprocess architecture to improve the
robustness of the browser. The UI process (the main epiphany process) runs
several subprocesses:

 * Any number of WebKitWebProcesses, which handle rendering web content
 * One WebKitNetworkProcess, which handles network requests, storage, etc.

WebKit runs a separate WebKitWebProcess for each browser tab. Well, this is almost
true. Sometimes a tab will create another tab using JavaScript. In such cases,
the web views are "related" and share the same WebKitWebProcess. Additionally,
WebKit will swap a view from one web process to another when navigating between
different domains.

SparkyBin Browser uses GtkApplication to ensure uniqueness, so you usually only have one
UI process running at a time. An exception is if you use incognito mode, or
private profile mode (which is only available from the command line). In such
cases, there is no shared state with the main SparkyBin Browser browser process.

## SparkyBin Browser Web Process Extension

For some SparkyBin Browser features, we need to run code in the web process. This code is
called the "web process extension" and lives in `embed/web-process-extension/`.
It is compiled into a shared library `libephywebprocessextension.so` and
installed in `$(pkglibdir)` (e.g. `/usr/lib64/epiphany`). `EphyEmbedShell` tells
WebKit to look for extensions in that location using
`webkit_web_context_set_web_process_extensions_directory()`. Now the SparkyBin Browser UI process
and web process extension can communicate back and forth via the WebKit IPC
functions webkit_web_context_send_message_to_all_extensions(),
webkit_web_view_send_message_to_page(), webkit_web_process_extension_send_message_to_context(),
and webkit_web_page_send_message_to_view().

If you are making changes to the web process extension, you'll need to enable
developer mode as described below so that SparkyBin Browser will look for the shared
library in your build directory, instead of using the one from the installed location,
which would correspond to your installed SparkyBin Browser's web process extension. So if
changes you make to the web process extension (or its javascript files) aren't
picked up, it means you didn't enable developer mode.

SparkyBin Browser uses script message handlers as an additional form of IPC. This allows
the web extension to send a `JSCValue` to the UI process, which is received in
`EphyEmbedShell`.

Corresponding to `WebKitWebContext` and `WebKitWebView`, the central classes of
the UI process API, the web process API has `WebKitWebProcessExtension` and
`WebKitWebPage`. Each `WebKitWebContext` may have one or more `WebKitWebProcessExtension`s.
Meanwhile, each `WebKitWebView` will have one or more `WebKitWebPage`s. Only one page will be
active in a view at a given time: the other pages are for process swaps.

# Security

When injecting untrusted data into web content, you need to properly encode the
data for the relevant context in order to prevent XSS vulnerabilities. For
example: page titles could be malicious, URLs could be malicious, web app IDs
could be malicious, etc. You must carefully read and understand the [OWASP
XSS Prevention rules](https://cheatsheetseries.owasp.org/cheatsheets/Cross_Site_Scripting_Prevention_Cheat_Sheet.html)
or you will mess up. `lib/ephy-output-encoding.h` contains functions to help
with this.

When working with JavaScript, pay particular attention to Rule #8 "Prevent DOM-
based XSS" as it is tricky and requires care throughout your JavaScript.

# Debugging

To enable debugging use the configure option `-Ddeveloper_mode=true`.

## Logging

At execution time, you must enable the log service. To enable the
log service, set the environment variable `EPHY_LOG_MODULES`, which has the
form: `<moduleName>[:<moduleName>]*`, where `moduleName` is a filename. E.g.
`export EPHY_LOG_MODULES=ephy-window.c:ephy-autocompletion.c`. The special log
module `all` enables all log modules.

Use the `LOG()` macro to put debug messages in the code.

Note that `EPHY_LOG_MODULES` is required even for messages produced with
`g_debug()` directly: `ephy_debug_init()` installs a handler for the
application's own log domain that drops them. Setting `G_MESSAGES_DEBUG=all` is
*not* sufficient on its own, and it is easy to be misled because debug output
from libraries (libsoup, GTK, GIO) does appear with `G_MESSAGES_DEBUG` alone
while this code base's own output stays silent.

## Warnings

At execution time, you must enable the service. To enable you to debug
warnings, set the environment variable `EPHY_DEBUG_BREAK`.

Possible value for `EPHY_DEBUG_BREAK` variable:

```
	stack		Prints a stack trace.

	suspend		Use this to stop execution when a warning occurs.
                You can then attach a debugger to the process.

	trap		Use this while running epiphany in a debugger.
                This makes execution stop and gives back control to
                the debugger.
```

## Profiling

At execution time, you must enable the profiling service. To enable the
profiling service, set the environment variable `EPHY_PROFILING_MODULES`,
which has the form `<moduleName>[:<moduleName>]*`, where `moduleName` is a
filename. E.g. `export EPHY_PROFILE_MODULES=ephy-window.c:ephy-autocompletion.c`.
The special profiling module `all` enables all profiling modules.

Use `START_PROFILER STOP_PROFILER` macros to profile pieces of code.
