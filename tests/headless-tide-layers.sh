#!/usr/bin/env bash
# The ways to start from Tide: with nothing configured Shady is Tide; a config
# can build on it (shady.base, shell.base, last binding wins); shady-rice eject
# makes a working copy; SHADY_RICE=none is the bare compositor and plain shell.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BUILD_DIR="${SHADY_SPATIAL_TEST_BUILD_DIR:-build-spatial-asan}"
for bin in shady shady-shell shady-rice; do
  [[ -e "$BUILD_DIR/$bin" ]] || { echo "tide-layers: missing $BUILD_DIR/$bin" >&2; exit 2; }
done
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
log=
fail() {
  [[ -n "$log" ]] && grep -E 'layer-test|shady-shell:|lua:|SHADY LUA|config.lua|rice|AddressSanitizer|runtime error' "$log" | tail -n 40 >&2
  echo "tide-layers: FAIL: $1" >&2
  exit 1
}

# run <name> <seconds> [VAR=value...]; the compositor runs without -c, under
# $tmp/<name>-home. The log is left in $log.
run() {
  local name="$1" seconds="$2"
  shift 2
  log="$tmp/$name.log"
  mkdir -p -m 700 "$tmp/$name"
  mkdir -p "$tmp/$name-home"
  (
    export XDG_RUNTIME_DIR="$tmp/$name" HOME="$tmp/$name-home"
    unset WAYLAND_DISPLAY WAYLAND_SOCKET SHADY_SOCKET SHADY_RICE SHADY_LUA_INIT \
      SHADY_SHELL_CONFIG XDG_CONFIG_HOME SHADY_ROOT
    export WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=gles2 LIBGL_ALWAYS_SOFTWARE=1
    export TIDE_IDLE_LOCK=0 TIDE_IDLE_SCREEN_OFF=0
    export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:halt_on_error=1:detect_leaks=0}"
    export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"
    [[ $# -gt 0 ]] && export "$@"
    timeout -s TERM "${seconds}s" "$BUILD_DIR/shady" || [[ $? -eq 124 ]]
  ) >"$log" 2>&1 || fail "$name: the compositor failed"
  ! grep -Eq 'AddressSanitizer|runtime error:' "$log" || fail "$name: sanitizer error"
  ! grep -Eq 'SHADY LUA\] (load|runtime) error|config.lua: (load|runtime) error|reload failed|falling back' "$log" \
    || fail "$name: a config did not load"
}
has() { grep -q -- "$1" "$log" || fail "$2"; }
hasnt() { ! grep -q -- "$1" "$log" || fail "$2"; }

# Nothing configured: Tide, with its shell.
run default 6
has 'loading rice: Tide' 'the default desktop is not Tide'
has 'shady-shell: loaded .*/rices/tide/shell.lua' 'the default shell is not Tide'

# The bare compositor stays available.
run none 4 SHADY_RICE=none
hasnt 'loading rice' 'SHADY_RICE=none loaded a rice'

# Another rice by name.
run other 6 SHADY_RICE=horizon
has 'loading rice: Horizon' 'SHADY_RICE=horizon did not load Horizon'
has 'shady-shell: loaded .*/rices/horizon/shell.lua' 'SHADY_RICE=horizon did not load its shell'

# A config built on Tide.
config="$tmp/layered-home/.config/shady"
mkdir -p "$config"
cat >"$config/config.lua" <<'LUA'
shady.base("tide")
shady.set("layout_gap", 22)
LUA
cat >"$config/init.lua" <<'LUA'
shady.base("tide")
shady.log("layer-test: base loaded")
-- Takes Tide's Super+q over, and removes Super+c.
shady.bind("Super+q", function() shady.log("layer-test: my close key") end, "Mine")
assert(shady.unbind("Super+c"), "Super+c was not bound")
assert(not shady.unbind("Super+c"), "Super+c is still bound")
shady.spawn([[sh -c 'exec "$SHADY_TEST_SHELL"']])
shady.automation.after(1500, function()
    shady.automation.key("Super+q")
end)
LUA
cat >"$config/shell.lua" <<'LUA'
local tide = shell.base("tide", {
    palette = { accent = "#FF7700" },
    bar = { right = function(ctx)
        shell.log("layer-test: bar extension")
        return shell.row { width = 10, height = 10 }
    end },
    launcher = function(query) return {} end,
})
assert(tide.P.accent == "#FF7700", "palette option ignored")
assert(tide.P.dim == "#8E92AE", "palette lost Tide's colours")
assert(tide.island and tide.card and tide.text, "pieces missing")
shell.log("layer-test: shell built on tide")
LUA
run layered 6 "SHADY_TEST_SHELL=$ROOT/$BUILD_DIR/shady-shell" "SHADY_ROOT=$ROOT"
has 'layer-test: base loaded' 'shady.base did not run'
has 'loading rice: Tide' 'shady.base("tide") did not run the config'
has 'layer-test: my close key' 'the later binding did not win'
has 'layer-test: shell built on tide' 'shell.base did not run'
has 'layer-test: bar extension' 'the bar option was not used'

# A copy of Tide that is the user's own.
rice="sh $ROOT/$BUILD_DIR/shady-rice"
home="$tmp/ejected-home"
mkdir -p "$home"
HOME="$home" XDG_CONFIG_HOME="" SHADY_ROOT="$ROOT" $rice eject >/dev/null || fail 'eject failed'
for file in config.lua init.lua shell.lua style.lua fonts shaders/tide.frag; do
  [[ -e "$home/.config/shady/$file" ]] || fail "eject did not copy $file"
done
if HOME="$home" XDG_CONFIG_HOME="" SHADY_ROOT="$ROOT" $rice eject >/dev/null 2>&1; then
  fail 'a second eject overwrote the first'
fi
sed -i '1i shell.log("layer-test: my own copy")' "$home/.config/shady/shell.lua"
printf '\nshady.spawn([[sh -c '"'"'exec "$SHADY_TEST_SHELL"'"'"']])\n' >>"$home/.config/shady/init.lua"
run ejected 6 "SHADY_TEST_SHELL=$ROOT/$BUILD_DIR/shady-shell"
has 'loading rice: Tide' 'the copy did not load'
has 'layer-test: my own copy' 'the shell did not load the copy'
hasnt 'no fonts in' 'the copy lost its fonts'

echo "tide-layers: PASS"
