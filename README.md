# SparkyBin Browser

SparkyBin Browser is a simple, clean web browser based on
[the WebKit rendering engine](https://webkit.org/), built with GTK 4 and
libadwaita.

It is a **rebranded fork of GNOME Web (Epiphany)**. The upstream project is
developed at <https://gitlab.gnome.org/GNOME/epiphany>.

## License and fork notice

SparkyBin Browser is free software, distributed under the terms of the **GNU
General Public License, version 3 or (at your option) any later version**.

This project is a modified version of GNOME Web (Epiphany). In accordance with
GPLv3 section 5(a), this file and [`NOTICE`](NOTICE) record that the source has
been modified and rebranded. Original copyright notices and the `COPYING` file
are retained. See [`NOTICE`](NOTICE) for details.

SparkyBin Browser is **not affiliated with, authorized by, sponsored by, or
endorsed by the GNOME Foundation or the GNOME Project**. "GNOME", "GTK",
"Flatpak", and "Flathub" are trademarks of the GNOME Foundation and are used
here only to factually describe the software this project is based upon.

## Download and Install

SparkyBin Browser is designed for Linux systems, and is intended to be
distributed via [Flatpak](https://www.flatpak.org/). Packaging and distribution
are configured by the project maintainers.

## Building from Source

### The Easy Way

The recommended way to build locally is using the flatpak-builder manifest with
GNOME Builder. After installing Builder and launching it, select **Open**, then
select the toplevel SparkyBin Browser directory. Builder will detect the
`com.sparkybin.Browser.json` flatpak-builder manifest and you will be able to
build the project. All required dependencies will be provided by the manifest.

### Building Manually

SparkyBin Browser uses the [Meson build system](http://mesonbuild.com/):

```
$ mkdir build && cd build
$ meson ..
$ ninja
$ sudo ninja install
```

You will have to install several pkg-config dependencies. If you are missing a
dependency, meson will present an error that looks like this:

```
meson.build:84:0: ERROR:  Native dependency 'hogweed' not found
```

In RPM-based distributions, you can install the missing dependencies
automatically. For example, in Fedora:

```
$ sudo dnf install 'pkgconfig(hogweed)'
```

In deb-based distributions:

```
$ sudo apt install $(apt-file search --package-only hogweed)
```

In other distributions, you must research each dependency to determine which
package provides the required pkg-config file.

### Rebuilding Dependencies

Newcomers should try to avoid bugs that require making changes in WebKit or
other dependencies. Once you are ready to make code changes in WebKit or other
dependencies, the recommended method is to use either
[Toolbox](https://containertoolbx.org/) or
[JHBuild](https://gnome.pages.gitlab.gnome.org/jhbuild/index.html). Here is an
example of how to build your own WebKit using JHBuild, assuming your JHBuild
installation prefix is `$HOME/jhbuild install/`:

```
$ git clone https://github.com/WebKit/WebKit.git WebKit
$ mkdir -p WebKit/WebKitBuild/GNOME
$ cd WebKit/WebKitBuild/GNOME
$ jhbuild run cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPORT=GTK -DDEVELOPER_MODE=ON -DCMAKE_INSTALL_PREFIX=$HOME/jhbuild/install/ -DCMAKE_INSTALL_LIBDIR=lib -GNinja ../..
$ jhbuild run ninja
$ jhbuild run cmake -P cmake_install.cmake
```

## Manifesto

A web browser is more than an application: it is a way of thinking, a way of
seeing the world. SparkyBin Browser's principles are simplicity, standards
compliance, and software freedom. These principles are inherited from the
upstream GNOME Web (Epiphany) project.

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

SparkyBin Browser is not just free of cost; more importantly, the source code is
made available to you under a license that
[respects your freedom](https://www.gnu.org/philosophy/philosophy.html).

## Human Interface

SparkyBin Browser follows the [GNOME Human Interface
Guidelines](https://developer.gnome.org/hig), like its upstream project.

## Reporting problems

Report bugs and feature requests for SparkyBin Browser via the project's own
tracker. Problems that also affect upstream GNOME Web should be reported to the
upstream project at <https://gitlab.gnome.org/GNOME/epiphany/issues>.
