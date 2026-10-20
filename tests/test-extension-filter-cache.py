#!/usr/bin/env python3
#
# Copyright © 2026 The SparkyBin Browser contributors
#
# This file is part of SparkyBin Browser.
#
# SparkyBin Browser is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# SparkyBin Browser is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with SparkyBin Browser.  If not, see <http://www.gnu.org/licenses/>.
"""
End-to-end test for the filter lists that an extension ships with.

The browser is started three times with the same profile:

  1. The list blocks /tracker.gif.  The page must load, the tracker must not
     be requested, and the list must be compiled.
  2. The list is unchanged.  It must still block, and the compiled rules must
     be loaded from the cache instead of being compiled again.
  3. The list is changed so that it no longer blocks the tracker.  It must be
     compiled again and the tracker must be requested.  Reusing the old rules
     here would keep blocking something the user removed, silently.

Only the Python standard library is used.  The driver starts its own Xvfb,
HTTP server and (through dbus-run-session) session bus, and stops all of them
before it exits.
"""

import argparse
import hashlib
import http.server
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

FILTER_READY_RE = re.compile(r"The filter list '([0-9a-f]{64})' is ready")
FILTER_COMPILED_RE = re.compile(r"The filter list '[^']*' has \d+ rules out of \d+ lines")
EXTENSION_ACTIVATED_RE = re.compile(r"Activating Test Blocker")

START_PAGE = b"""<!doctype html>
<html><head><meta charset="utf-8"><title>Starting</title></head>
<body>
<p>Waiting until the browser has loaded the extension's filter list.</p>
<script>
(function () {
  function poll() {
    fetch('/go', {cache: 'no-store'}).then(function (response) {
      if (response.ok)
        window.location.replace('/page');
      else
        window.setTimeout(poll, 200);
    }).catch(function () {
      window.setTimeout(poll, 200);
    });
  }
  poll();
})();
</script>
</body></html>
"""

PAGE = b"""<!doctype html>
<html><head><meta charset="utf-8"><title>A page</title></head>
<body>
<h1>A page</h1>
<img src="/tracker.gif" alt="tracker">
<p>The image above should not reach the server.</p>
</body></html>
"""

TRACKER_GIF = (b"GIF89a\x01\x00\x01\x00\x80\x00\x00\x00\x00\x00\xff\xff\xff"
               b"!\xf9\x04\x01\x00\x00\x00\x00,\x00\x00\x00\x00\x01\x00\x01"
               b"\x00\x00\x02\x02D\x01\x00;")


class TestFailure(Exception):
    pass


class RequestServer:
    """A local HTTP server that records every path it is asked for."""

    def __init__(self):
        self._lock = threading.Lock()
        self._requests = []
        self._go = threading.Event()
        self._httpd = None
        self._thread = None
        self.port = None
        self._start()

    def _start(self):
        owner = self

        class Handler(http.server.BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"

            def do_GET(self):
                owner._record(self.path)

                if self.path == "/start":
                    self._send(200, "text/html; charset=utf-8", START_PAGE)
                elif self.path == "/go":
                    if owner._go.is_set():
                        self._send(200, "text/plain; charset=utf-8", b"go\n")
                    else:
                        self._send(503, "text/plain; charset=utf-8", b"wait\n")
                elif self.path == "/page":
                    self._send(200, "text/html; charset=utf-8", PAGE)
                elif self.path == "/tracker.gif":
                    self._send(200, "image/gif", TRACKER_GIF)
                else:
                    self._send(404, "text/plain; charset=utf-8", b"not found\n")

            def _send(self, status, content_type, body):
                self.send_response(status)
                self.send_header("Content-Type", content_type)
                self.send_header("Content-Length", str(len(body)))
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                self.wfile.write(body)

            def log_message(self, *args):
                pass

        self._httpd = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self._httpd.daemon_threads = True
        self.port = self._httpd.server_address[1]
        self._thread = threading.Thread(target=self._httpd.serve_forever, daemon=True)
        self._thread.start()

    def _record(self, path):
        with self._lock:
            self._requests.append(path)

    def stop(self):
        if self._httpd is not None:
            self._httpd.shutdown()
            self._httpd.server_close()
            self._httpd = None
        if self._thread is not None:
            self._thread.join(timeout=5)
            self._thread = None

    def release_page(self):
        self._go.set()

    def mark(self):
        with self._lock:
            return len(self._requests)

    def paths_since(self, mark):
        with self._lock:
            return list(self._requests[mark:])

    def wait_for(self, path, mark, timeout):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if path in self.paths_since(mark):
                return True
            time.sleep(0.05)
        return False


class Xvfb:
    """A private X server, so the test does not need a display."""

    def __init__(self):
        binary = shutil.which("Xvfb")
        if binary is None:
            raise TestFailure("Xvfb was not found; install the Xvfb package")

        for number in range(90, 200):
            socket_path = Path("/tmp/.X11-unix/X%d" % number)
            lock_path = Path("/tmp/.X%d-lock" % number)
            if socket_path.exists() or lock_path.exists():
                continue

            self.display = ":%d" % number
            self._proc = subprocess.Popen(
                [binary, self.display, "-screen", "0", "1280x800x24",
                 "-nolisten", "tcp", "-ac"],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                start_new_session=True,
            )

            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                if socket_path.exists():
                    return
                if self._proc.poll() is not None:
                    break
                time.sleep(0.05)

            self.stop()
            raise TestFailure("Xvfb did not start on %s" % self.display)

        raise TestFailure("no free X display was found")

    def stop(self):
        stop_process_tree(self._proc)


def stop_process_tree(proc):
    if proc is None:
        return

    if proc.poll() is None:
        try:
            os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
        except ProcessLookupError:
            pass

        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
            except ProcessLookupError:
                pass
            proc.wait(timeout=5)


def tail(text, lines=80):
    split = text.splitlines()
    if len(split) > lines:
        split = split[-lines:]
    return "\n".join(split)


def read_text(path):
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except FileNotFoundError:
        return ""


def wait_for_log(proc, log_path, pattern, timeout, description):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        text = read_text(log_path)
        match = pattern.search(text)
        if match:
            return match, text
        if proc.poll() is not None:
            raise TestFailure("the browser exited with status %s before %s:\n%s"
                              % (proc.returncode, description, tail(text)))
        time.sleep(0.1)

    raise TestFailure("timed out waiting for %s:\n%s"
                      % (description, tail(read_text(log_path))))


def snapshot_cache(cache_dir, identifier=None):
    if not cache_dir.is_dir():
        return {}

    snapshot = {}
    for path in sorted(cache_dir.rglob("*")):
        if not path.is_file():
            continue
        if identifier is not None and identifier not in path.name:
            continue
        stat = path.stat()
        snapshot[str(path.relative_to(cache_dir))] = (
            stat.st_size,
            stat.st_mtime_ns,
            hashlib.sha256(path.read_bytes()).hexdigest(),
        )
    return snapshot


def describe_cache(snapshot):
    if not snapshot:
        return "    (empty)"
    lines = []
    for name, (size, mtime, digest) in sorted(snapshot.items()):
        lines.append("    %s size=%d mtime=%d sha256=%s"
                     % (name, size, mtime, digest))
    return "\n".join(lines)


class FilterCacheTest:
    def __init__(self, args):
        self.args = args
        self.root = Path(tempfile.mkdtemp(prefix="sparkybin-filter-cache-"))
        self.data_home = self.root / "data"
        self.config_home = self.root / "config"
        self.cache_home = self.root / "cache"
        self.home = self.root / "home"
        self.runtime_dir = self.root / "runtime"
        self.extension_dir = (self.data_home / "com.sparkybin.Browser" /
                              "web_extensions" / "test-blocker")
        self.profile_dir = self.data_home / "com.sparkybin.Browser"
        self.cache_dir = (self.cache_home / "com.sparkybin.Browser" /
                          "adblock" / "compiled")
        self.keyfile = self.config_home / "glib-2.0" / "settings" / "keyfile"

        for directory in (self.data_home, self.config_home, self.cache_home,
                          self.home, self.runtime_dir,
                          self.keyfile.parent, self.cache_dir):
            directory.mkdir(parents=True, exist_ok=True)
        self.runtime_dir.chmod(0o700)

        self.xvfb = None
        self.server = None
        self.logs = []
        self.run_number = 0

    def close(self):
        if self.server is not None:
            self.server.stop()
            self.server = None
        if self.xvfb is not None:
            self.xvfb.stop()
            self.xvfb = None
        if self.args.keep:
            print("kept the test profile at %s" % self.root)
        else:
            shutil.rmtree(self.root, ignore_errors=True)

    def setup(self):
        self.xvfb = Xvfb()
        self.server = RequestServer()
        self.install_extension("||127.0.0.1:%d/tracker.gif" % self.server.port)
        self.mark_profile_as_migrated()

    def mark_profile_as_migrated(self):
        """Do not spawn the installed profile migrator.

        A build tree has the migrator next to the browser, but the browser
        only looks there in a developer-mode build.  Seeding the profile with
        the current migration version makes it skip the migration, the same
        way a profile that has already been opened does.  The version comes
        from the source so it does not have to be kept in step by hand.
        """
        source_root = Path(self.args.fixture).resolve().parents[2]
        header = source_root / "lib" / "ephy-profile-utils.h"
        match = re.search(r"^#define\s+EPHY_PROFILE_MIGRATION_VERSION\s+(\d+)\s*$",
                          read_text(header), re.MULTILINE)
        if match is None:
            raise TestFailure("could not find the migration version in %s" % header)

        (self.profile_dir / ".migrated").write_text(match.group(1),
                                                     encoding="utf-8")

    def install_extension(self, rule):
        if self.extension_dir.exists():
            shutil.rmtree(self.extension_dir)
        shutil.copytree(self.args.fixture, self.extension_dir)

        filter_file = self.extension_dir / "assets" / "ublock" / "test.txt"
        text = filter_file.read_text(encoding="utf-8")
        text = text.replace("__PORT__", str(self.server.port))
        text = text.replace("||127.0.0.1:%d/tracker.gif" % self.server.port,
                            rule)
        filter_file.write_text(text, encoding="utf-8")
        return text

    def write_settings(self):
        adblock = "false" if self.args.disable_adblock else "true"
        self.keyfile.write_text(
            "[com/sparkybin/browser]\n"
            "homepage-url='about:blank'\n"
            "restore-session-policy='crashed'\n"
            "content-filters=@as []\n"
            "\n"
            "[com/sparkybin/browser/web]\n"
            "enable-adblock=%s\n"
            "enable-webextensions=true\n"
            "webextensions-active=['Test Blocker']\n" % adblock,
            encoding="utf-8",
        )

    def browser_environment(self):
        env = os.environ.copy()
        env.update({
            "HOME": str(self.home),
            "XDG_DATA_HOME": str(self.data_home),
            "XDG_CONFIG_HOME": str(self.config_home),
            "XDG_CACHE_HOME": str(self.cache_home),
            "XDG_RUNTIME_DIR": str(self.runtime_dir),
            "GSETTINGS_BACKEND": "keyfile",
            "GDK_BACKEND": "x11",
            "DISPLAY": self.xvfb.display,
            "WEBKIT_DISABLE_COMPOSITING_MODE": "1",
            "WEBKIT_DISABLE_DMABUF_RENDERER": "1",
            "WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS": "1",
            "LIBGL_ALWAYS_SOFTWARE": "1",
            "EPHY_LOG_MODULES": "filter list:Activating",
            "NO_AT_BRIDGE": "1",
            "GTK_A11Y": "none",
        })

        if "GSETTINGS_SCHEMA_DIR" not in env:
            build_root = Path(self.args.browser).resolve().parent.parent
            env["GSETTINGS_SCHEMA_DIR"] = str(build_root / "data")

        return env

    def clear_session(self):
        profile = self.data_home / "com.sparkybin.Browser"
        for name in ("session_state.xml", "session_state.xml.bak"):
            try:
                (profile / name).unlink()
            except FileNotFoundError:
                pass

    def start_browser(self):
        self.run_number += 1
        log_path = self.root / ("browser-%d.log" % self.run_number)
        log_file = open(log_path, "wb")

        command = []
        dbus_run_session = shutil.which("dbus-run-session")
        if dbus_run_session is not None:
            command += [dbus_run_session, "--"]
        command += [self.args.browser,
                    "http://127.0.0.1:%d/start" % self.server.port]

        try:
            proc = subprocess.Popen(
                command,
                stdout=log_file,
                stderr=subprocess.STDOUT,
                env=self.browser_environment(),
                cwd=str(self.root),
                start_new_session=True,
            )
        finally:
            log_file.close()

        self.logs.append((log_path, proc))
        return proc, log_path

    def run_once(self, name, rule, should_block, should_compile):
        print("=== %s ===" % name)
        self.clear_session()
        text = self.install_extension(rule)
        self.write_settings()

        mark = self.server.mark()
        proc, log_path = self.start_browser()

        try:
            if not self.server.wait_for("/start", mark, 30):
                raise TestFailure("the browser never requested the start page:\n%s"
                                  % tail(read_text(log_path)))

            if self.args.disable_adblock:
                # The deliberate-break mode.  The point is to reach the page
                # and show that the tracker is requested, not to wait for a
                # filter list that the setting says not to build.
                wait_for_log(proc, log_path, EXTENSION_ACTIVATED_RE, 30,
                             "the extension to be activated")
                time.sleep(3)
                expected_identifier = None
            else:
                match, _ = wait_for_log(proc, log_path, FILTER_READY_RE, 30,
                                        "the filter list to be ready")
                expected_identifier = hashlib.sha256(text.encode("utf-8")).hexdigest()
                if match.group(1) != expected_identifier:
                    raise TestFailure("the filter list was stored under %s, expected %s"
                                      % (match.group(1), expected_identifier))

            self.server.release_page()

            if not self.server.wait_for("/page", mark, 30):
                raise TestFailure("the browser never requested the page after the "
                                  "filter list was ready:\n%s"
                                  % tail(read_text(log_path)))

            if should_block:
                # The image is part of the page, so it has had plenty of time
                # to be requested by now.
                time.sleep(2)
            elif not self.server.wait_for("/tracker.gif", mark, 10):
                raise TestFailure("the tracker was not requested although the "
                                  "filter list no longer blocks it")

            if proc.poll() is not None:
                raise TestFailure("the browser exited with status %s:\n%s"
                                  % (proc.returncode, tail(read_text(log_path))))

            log = read_text(log_path)
            requests = self.server.paths_since(mark)
            tracker_requested = "/tracker.gif" in requests
            compiled = bool(FILTER_COMPILED_RE.search(log))

            print("  requests: %s" % " ".join(requests))
            print("  compiled: %s" % ("yes" if compiled else "no"))

            if should_block and tracker_requested:
                raise TestFailure("the tracker was requested although the filter "
                                  "list blocks it:\n%s" % tail(log))
            if not should_block and not tracker_requested:
                raise TestFailure("the tracker was not requested after the blocking "
                                  "rule was removed; the stale rules were reused:\n%s"
                                  % tail(log))
            if should_compile and not compiled:
                raise TestFailure("the filter list was not compiled:\n%s" % tail(log))
            if not should_compile and compiled:
                raise TestFailure("the filter list was compiled again instead of "
                                  "being loaded from the cache:\n%s" % tail(log))

            return {
                "log": log,
                "identifier": expected_identifier,
                "compiled": compiled,
                "cache": snapshot_cache(self.cache_dir, expected_identifier),
            }
        finally:
            stop_process_tree(proc)

    def run(self):
        self.setup()

        blocking_rule = "||127.0.0.1:%d/tracker.gif" % self.server.port
        other_rule = "||127.0.0.1:%d/something-else.gif" % self.server.port

        first = self.run_once("the first start", blocking_rule,
                              should_block=True, should_compile=True)
        if not first["cache"]:
            raise TestFailure("no compiled rules were left for the filter list in %s"
                              % self.cache_dir)
        print("  cached rules:\n%s" % describe_cache(first["cache"]))

        second = self.run_once("the second start, same list", blocking_rule,
                               should_block=True, should_compile=False)
        if second["cache"] != first["cache"]:
            raise TestFailure("the compiled rules changed on the second start, "
                              "so they were not reused:\nbefore:\n%s\nafter:\n%s"
                              % (describe_cache(first["cache"]),
                                 describe_cache(second["cache"])))

        third = self.run_once("the third start, changed list", other_rule,
                              should_block=False, should_compile=True)
        if third["cache"] == second["cache"]:
            raise TestFailure("the changed list did not produce new compiled rules")

        print("the tracker was blocked on the first two starts, loaded from the "
              "cache on the second, and requested after the list changed")


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--browser", required=True,
                        help="the sparkybin-browser binary from the build tree")
    parser.add_argument("--fixture", required=True,
                        help="the stub extension shipped with the test")
    parser.add_argument("--disable-adblock", action="store_true",
                        help="turn the ad blocking setting off; used to prove "
                             "that the test fails when the tracker is requested")
    parser.add_argument("--keep", action="store_true",
                        help="keep the temporary profile and logs")
    return parser.parse_args(argv)


def main(argv):
    args = parse_args(argv)
    test = FilterCacheTest(args)

    try:
        test.run()
    except TestFailure as error:
        print("FAIL: %s" % error, file=sys.stderr)
        return 1
    finally:
        test.close()

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
