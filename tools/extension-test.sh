#!/bin/sh
# Run the browser once for each of the extensions that were fetched, and
# report how far each one got.
#
#   tools/extension-test.sh [directory]
#
# The directory is where tools/fetch-extensions.py put them. The browser has
# to be built and installed already, and there has to be a display.

set -e

HERE=$(dirname "$0")
EXT_DIR=${1:-/tmp/extensions}
BROWSER=${BROWSER:-sparkybin-browser}
SECONDS_PER_EXTENSION=${SECONDS_PER_EXTENSION:-24}

if [ ! -d "$EXT_DIR" ]; then
  echo "No extensions in $EXT_DIR. Run tools/fetch-extensions.py first." >&2
  exit 1
fi

PROFILE=/tmp/extension-test-profile
LOG=/tmp/extension-test.log

run_one () {
  EXT=$1
  NAME=$2

  rm -rf "$PROFILE"
  mkdir -p "$PROFILE/data/com.sparkybin.Browser/web_extensions"
  mkdir -p "$PROFILE/config/glib-2.0/settings"
  cp -r "$EXT" "$PROFILE/data/com.sparkybin.Browser/web_extensions/"

  cat > "$PROFILE/config/glib-2.0/settings/keyfile <<EOF
[com/sparkybin/browser]
ask-for-default=false

[com/sparkybin/browser/web]
enable-webextensions=true
webextensions-active=['$NAME']
EOF

  XDG_DATA_HOME=$PROFILE/data \
  XDG_CONFIG_HOME=$PROFILE/config \
  XDG_CACHE_HOME=$PROFILE/cache \
  GSETTINGS_BACKEND=keyfile \
  EPHY_LOG_MODULES=all \
  "$BROWSER" "https://example.test/" > "$LOG" 2>&1 &

  BROWSER_PID=$!
  sleep "$SECONDS_PER_EXTENSION"
  kill $BROWSER_PID 2>/dev/null || true
  wait $BROWSER_PID 2>/dev/null || true

  ACTIVATED=$(grep -c "Activating $NAME" "$LOG" || true)
  SERVED=$(grep -c "Serving '" "$LOG" || true)
  API=$(grep -c "Called for $NAME" "$LOG" || true)
  JSERR=$(grep -c 'CONSOLE JS ERROR' "$LOG" || true)
  CRIT=$(grep -cE 'CRITICAL' "$LOG" || true)

  printf "%-30s %-6s %-6s %-7s %-6s %-6s %-6s\n" \
    "$NAME" "$3" "$ACTIVATED" "$SERVED" "$API" "$JSERR" "$CRIT"

  grep -E "CONSOLE JS ERROR|CRITICAL" "$LOG" \
    | grep -viE "vulkan|portal|atspi|MIME type|g_object_unref" \
    | head -2 | sed 's/^/      /'
}

printf "%-30s %-6s %-6s %-7s %-6s %-6s %-6s\n" \
  "extension" "ver" "on" "served" "api" "jserr" "crit"

for dir in "$EXT_DIR"/*/; do
  dir=${dir%/}
  [ -f "$dir/manifest.json" ] || continue

  name=$(python3 "$HERE/extension-name.py" "$dir")
  mv=$(python3 -c "import json;print('MV'+str(json.load(open('$dir/manifest.json')).get('manifest_version')))")

  run_one "$dir" "$name" "$mv"
done
