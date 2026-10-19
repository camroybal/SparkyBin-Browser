#!/bin/sh
# Measure how long the browser takes to put a window on the screen, and how
# much memory it and its web processes are using once it is there.
#
#   tools/measure.sh build/src/sparkybin-browser [URL]
#
# Needs xdotool to find the window. Without it, only the memory and process
# counts are printed.

set -eu

BROWSER=${1:-build/src/sparkybin-browser}
URL=${2:-about:blank}
SETTLE=${SETTLE:-10}

if [ ! -x "$BROWSER" ]; then
  echo "$BROWSER is not executable" >&2
  exit 1
fi

command -v xdotool >/dev/null 2>&1 || echo "note: no xdotool, so no startup time" >&2

# A scratch profile, so the numbers do not depend on what is in the real one.
PROFILE=$(mktemp -d)
export XDG_DATA_HOME="$PROFILE/data"
export XDG_CONFIG_HOME="$PROFILE/config"
export XDG_CACHE_HOME="$PROFILE/cache"
mkdir -p "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME"

START=$(date +%s.%N)

"$BROWSER" --new-window "$URL" &
BROWSER_PID=$!

# Wait for the window to appear, and note when it did.
WINDOW_AT=""
if command -v xdotool >/dev/null 2>&1; then
  i=0
  while [ "$i" -lt 100 ]; do
    if xdotool search --class "$(basename "$BROWSER")" >/dev/null 2>&1; then
      WINDOW_AT=$(date +%s.%N)
      break
    fi
    sleep 0.1
    i=$((i + 1))
  done
fi

sleep "$SETTLE"

if [ -n "$WINDOW_AT" ]; then
  echo "time to window: $(awk "BEGIN { printf \"%.0f ms\", ($WINDOW_AT - $START) * 1000 }")"
else
  echo "time to window: not measured"
fi

# The browser and every web process it started.
RSS=$(ps -o rss= -p "$BROWSER_PID" 2>/dev/null | tr -d ' ' || echo 0)
PROCESSES=1

for pid in $(pgrep -P "$BROWSER_PID" 2>/dev/null || true); do
  child_rss=$(ps -o rss= -p "$pid" 2>/dev/null | tr -d ' ' || echo 0)
  RSS=$((RSS + child_rss))
  PROCESSES=$((PROCESSES + 1))
  for grandchild in $(pgrep -P "$pid" 2>/dev/null || true); do
    gc_rss=$(ps -o rss= -p "$grandchild" 2>/dev/null | tr -d ' ' || echo 0)
    RSS=$((RSS + gc_rss))
    PROCESSES=$((PROCESSES + 1))
  done
done

echo "processes: $PROCESSES"
echo "memory:    $((RSS / 1024)) MiB"

kill "$BROWSER_PID" 2>/dev/null || true
wait "$BROWSER_PID" 2>/dev/null || true
rm -rf "$PROFILE"
