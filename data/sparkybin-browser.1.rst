========
sparkybin-browser
========

------------------------------------------
A simple, clean, beautiful view of the web
------------------------------------------

:Date: May 2025
:Copyright: Copyright 2006-2025 GNOME Foundation
:Copyright: Copyright 2026 SparkyBin
:Manual section: 1

SYNOPSIS
--------

|  **sparkybin-browser** [*OPTION*...] [*URL*...]

DESCRIPTION
-----------

**sparkybin-browser** is the SparkyBin Browser, a web browser based on the
WebKitGTK rendering engine. It is a rebranded fork of GNOME Web (Epiphany).

OPTIONS
-------

``--new-window``

  Open a new browser window instead of a new tab

``-l``, ``--load-session``\ =\ *FILE*

  Load the given session state file

``-i``, ``--incognito-mode``

  Start an instance with read-only user data, for browsing without storing history on your computer

``-p``, ``--private-instance``

  Start a temporary instance without any user data, for testing

``-a``, ``--application-mode``\ =\ *BASENAME*

  Start a private instance in web application mode (requires passing either desktop file basename or ``--profile``)

``--automation-mode``

  Start a private instance for WebDriver control

``--profile``\ =\ *FILE*

  Start using a custom profile directory for user data

``--search``\ =\ *TERM*

  Initiate search with given search term

``-?``, ``--help``

  Show help options

``--version``

  Show version
  
SparkyBin Browser has a comprehensive help system.  Run the browser
and select ``Help`` from the menu.

AUTHOR
------

SparkyBin Browser is a fork of GNOME Web (Epiphany), which has been developed
by Marco Pesenti Gritti (in memoriam), Christian Persch, Xan Lopez, Carlos
Garcia Campos, Claudio Saavedra, Michael Catanzaro, Jan-Michael Brummer, Alice
Mikhaylenko, Patrick Griffis, and many others.
