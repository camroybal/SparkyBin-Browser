# SparkyBin Browser

A web browser for the desktop, built on WebKitGTK with GTK 4 and libadwaita.

SparkyBin Browser is a maintained build of
[GNOME Web](https://gitlab.gnome.org/GNOME/epiphany), last synced with the
51.1 release. It is an independent project and is not affiliated with or
endorsed by the GNOME Foundation.

[Download](#download) ·
[Source](https://github.com/camroybal/SparkyBin-Browser) ·
[Report a bug](https://github.com/camroybal/SparkyBin-Browser/issues) ·
[Website](https://camroybal.github.io/SparkyBin-site)

## What is different

The engine and the interface are GNOME Web's. The differences are in the
defaults and in the parts that needed work:

- **WebExtensions are on by default.** Upstream treats them as experimental
  and hides them behind a setting.
- **The WebExtension code has been audited.** Crashes, use-after-frees and a
  few security problems were fixed along the way, among them a JavaScript
  injection in `downloads.search()`, a path traversal in
  `downloads.download()`, and the extension API being handed to frames that do
  not belong to the extension.
- **The identifiers are ours.** The application ID, the executable name and
  the user data directories are all different, so a SparkyBin Browser
  installation and a GNOME Web installation do not share history, bookmarks,
  cookies or extensions.

The complete list of changes is in [NOTICE](NOTICE), which records them as
GPLv3 section 5(a) requires. The WebExtension implementation is described in
[src/webextension/README.md](src/webextension/README.md), including what is
not finished yet.

## Download

Releases are published on
[GitHub](https://github.com/camroybal/SparkyBin-Browser/releases).

| Channel | What it is | Where |
| --- | --- | --- |
| Stable | A tagged release. This is the one to install. | [Releases](https://github.com/camroybal/SparkyBin-Browser/releases) |
| Devel | The tip of `main`, built against the WebKit in the GNOME runtime. | Not published yet |
| Canary | The tip of `main`, built against WebKit `main`. Breaks sometimes. | Not published yet |

A release is a flatpak bundle. To install one:

```sh
flatpak install --user -y --bundle sparkybin-browser-v0.1.flatpak
```

Or, from a terminal in a SparkyBin Browser checkout, after
[building it](#building-from-source):

```sh
build/src/sparkybin-browser --new-window
```

There is no package for your distribution yet. If you want one, say so on the
[issue tracker](https://github.com/camroybal/SparkyBin-Browser/issues).

## Building from source

The recommended way to build locally is using the flatpak-builder manifest
with GNOME Builder. After installing Builder and launching it, select
**Open**, then select the toplevel SparkyBin Browser directory. Builder will
detect the flatpak-builder manifests and you will be able to build the
project. All required dependencies will be provided by the manifest.

There are three manifests, one per channel:

| Manifest | Application ID | WebKit |
| --- | --- | --- |
| `com.sparkybin.Browser.json` | `com.sparkybin.Browser` | The GNOME runtime, version 51 |
| `com.sparkybin.Browser.Devel.json` | `com.sparkybin.Browser.Devel` | The GNOME runtime, `master` |
| `com.sparkybin.Browser.Canary.json` | `com.sparkybin.Browser.Canary` | WebKit `main` |

### Building manually

SparkyBin Browser uses the [Meson build system](http://mesonbuild.com/):

```sh
mkdir build && cd build
meson ..
ninja
sudo ninja install
```

You will have to install several pkg-config dependencies. If you are missing a
dependency, meson will present an error that looks like this:

```
meson.build:84:0: ERROR:  Native dependency 'hogweed' not found
```

In RPM-based distributions, you can install the missing dependencies
automatically. For example, in Fedora:

```sh
sudo dnf install 'pkgconfig(hogweed)'
```

In deb-based distributions:

```sh
sudo apt install $(apt-file search --package-only hogweed)
```

In other distributions, you must research each dependency to determine which
package provides the required pkg-config file.

[HACKING.md](HACKING.md) has a container recipe that needs nothing installed
on the host, along with how to run the tests.

### Rebuilding dependencies

Newcomers should try to avoid bugs that require making changes in WebKit or
other dependencies. Once you are ready to make code changes in WebKit or other
dependencies, the recommended method is to use either
[Toolbox](https://containertoolbx.org/) or
[JHBuild](https://gnome.pages.gitlab.gnome.org/jhbuild/index.html). Here is an
example of how to build your own WebKit using JHBuild, assuming your JHBuild
installation prefix is `$HOME/jhbuild install/`:

```sh
$ git clone https://github.com/WebKit/WebKit.git WebKit
$ mkdir -p WebKit/WebKitBuild/GTK
$ cd WebKit/WebKitBuild/GTK
$ jhbuild run cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPORT=GTK -DDEVELOPER_MODE=ON -DCMAKE_INSTALL_PREFIX=$HOME/jhbuild/install/ -DCMAKE_INSTALL_LIBDIR=lib -GNinja ../..
$ jhbuild run ninja
$ jhbuild run cmake -P cmake_install.cmake
```

## Reporting problems

Report bugs and feature requests on the
[issue tracker](https://github.com/camroybal/SparkyBin-Browser/issues).
[CONTRIBUTING.md](CONTRIBUTING.md) says what to include, and which problems
belong to WebKit rather than to the browser.

Problems that also affect upstream GNOME Web should be reported there as
well, at <https://gitlab.gnome.org/GNOME/epiphany/issues>.

## Manifesto

A web browser is more than an application: it is a way of thinking, a way of
seeing the world. SparkyBin Browser's principles are simplicity, standards
compliance, and software freedom. These principles are inherited from the
upstream GNOME Web project.

### Simplicity

Feature bloat and user interface clutter is evil.

SparkyBin Browser aims to present the simplest interface possible for a browser.
Simple does not necessarily mean less-powerful. The UNIX philosophy is to design
small tools that do one thing and do it well.

### Standards Compliance

Standards compliance ensures the freedom of choice. The introduction of
nonstandard features in browsers could make it difficult or impossible to use
alternative products if developers embrace them.

### Software Freedom

SparkyBin Browser is not just free of cost; more importantly, the source code
is made available to you under a license that
[respects your freedom](https://www.gnu.org/philosophy/philosophy.html).

## Human Interface

SparkyBin Browser follows the [GNOME Human Interface
Guidelines](https://developer.gnome.org/hig), like its upstream project.

## License

SparkyBin Browser is free software, distributed under the terms of the **GNU
General Public License, version 3 or (at your option) any later version**.

This project is a modified version of GNOME Web (Epiphany). In accordance with
GPLv3 section 5(a), this file and [`NOTICE`](NOTICE) record that the source has
been modified and rebranded. Original copyright notices and the `COPYING` file
are retained. See [`NOTICE`](NOTICE) for details.

"GNOME", "GTK", "Flatpak", and "Flathub" are trademarks of the GNOME Foundation
and are used here only to factually describe the software this project is based
upon.
