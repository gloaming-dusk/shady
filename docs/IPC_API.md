# IPC API

Shady listens on a Unix socket so that shells, bars and scripts can read
compositor state, send commands and follow events without being Lua or a
native plugin. It is the foundation for the scriptable shell described in
[shady-shell's design notes](https://github.com/gloaming-dusk/shady-shell/blob/main/docs/SHELL_DESIGN.md), and it is equally usable from any
language that can open a socket.

## Socket

```text
$XDG_RUNTIME_DIR/shady-<wayland socket>.sock    e.g. /run/user/1000/shady-wayland-0.sock
```

The compositor exports the path as `SHADY_SOCKET`, so programs started by
Shady (`shady.spawn`, `-s`) find it directly. The socket is created with
mode `0700` and removed on exit. If `XDG_RUNTIME_DIR` is unset the
compositor runs without IPC.

## Wire format

Newline-delimited JSON in both directions. Each request is one JSON object
on one line:

```json
{"id": 1, "cmd": "windows"}
```

- `cmd` (string) selects the command. Other fields are its arguments.
- `id` (number or string, optional) is echoed in the response so clients
  can match replies. It is never a command argument.
- Argument values are strings, numbers, booleans, `null` or arrays of
  strings. Nested objects are rejected. A request line may be up to 64 KiB.

Every request gets exactly one response line, in order. A few commands
answer only once something has happened (`screenshot` with `wait`,
`pointer.drag` with a `duration`, `wait`); the requests after one of them on
the same connection run after its answer, so the order still holds:

```json
{"id": 1, "ok": true, "data": [ ... ]}
{"id": 2, "ok": false, "error": "no such window"}
```

`data` is omitted for commands that return nothing. A malformed request is
answered with an error and the connection stays open.

## Objects

**Window**

```json
{"id": 3, "app_id": "foot", "title": "~", "workspace": "main",
 "focused": true, "mapped": true, "maximized": false, "fullscreen": false,
 "x": 120, "y": 80, "width": 800, "height": 600}
```

`id` is stable for the window's lifetime and matches the id used by the
`shady-shell-v1` protocol. Only mapped windows are listed.

**Output**

```json
{"name": "HEADLESS-1", "width": 1280, "height": 720, "scale": 1, "enabled": true}
```

## Queries

| Command | Data |
|---|---|
| `version` | `{"protocol": 1, "compositor": "shady"}` |
| `windows` | array of windows |
| `focused` | the focused window or `null` |
| `workspaces` | `{"current": "main", "list": ["main", "two"]}` |
| `outputs` | array of outputs |
| `plugins` | array of `{"name", "spec", "state", "builtin"}`, as `shady.plugins.list()` |
| `values` | every published value, as `{"key": "value", ...}` |
| `value` | `{"key": ...}` -> that value, or `null` |
| `shortcuts` | array of `{"keys", "group", "description"}`: the shortcut overlay's list (see [LUA_API.md](LUA_API.md#shortcut-overlay)) |

## Commands

| Command | Arguments | Effect |
|---|---|---|
| `window.focus` | `window` | Focus the window, switching to its workspace first |
| `window.close` | `window` | Ask the client to close |
| `window.maximize` | `window`, `state`? | Set maximized; toggles when `state` is absent |
| `window.fullscreen` | `window`, `state`? | Set fullscreen; toggles when `state` is absent |
| `window.move` | `window`, `workspace` | Move to a workspace, creating it if needed |
| `workspace.switch` | `name` | Switch workspace, creating it if needed |
| `key` | `keys` | Press and release a shortcut such as `"Super+Shift+z"`; data is `{"handled": bool, "sent": bool}` (see [Input](#input)) |
| `key.down`, `key.up` | `keys` | Only press it, or only release it |
| `type` | `text` | Type UTF-8 text into the focused window; data is `{"typed": characters}` |
| `pointer` | | `{"x", "y", "window"}`: where the pointer is, and the window `id` under it or `null` |
| `pointer.move` | `x`, `y` | Move the pointer |
| `pointer.click` | `x`?, `y`?, `button`?, `count`? | Move there, then click `count` (1–3) times |
| `pointer.down`, `pointer.up` | `x`?, `y`?, `button`? | Press or release a button and leave it so |
| `pointer.scroll` | `dx`?, `dy`?, `x`?, `y`? | Scroll by wheel clicks; positive is down and right |
| `pointer.drag` | `x1`, `y1`, `x2`, `y2`, `button`?, `steps`?, `duration`? | Press, move in `steps` (10), release, over `duration` ms (300; `0` all at once); answers once released |
| `window.at` | `x`, `y` | The window a click there would reach, or `null` |
| `wait` | `ms`?, `idle`?, `timeout`?, `scope`?, `window`? | Answer after `ms`, then once nothing has changed for `idle` ms, or after `timeout` ms (5000); data is `{"settled": bool, "elapsed": ms, "active"?: what changed last}` (see [Input](#input)) |
| `screenshot` | `path`?, `delay`?, `wait`?, `output`?, `window`?, `x`/`y`/`width`/`height`?, `max_width`?, `max_height`? | Save the next frame as a PNG (default `~/Pictures/shady-<time>.png`) after `delay` ms; see [Screenshots](#screenshots) |
| `shortcuts.toggle` | | Ask shells to show or hide the shortcut overlay (the `shortcuts.toggle` event) |
| `lua` | `code`, `name`? | Execute Lua in the live runtime; data is an array of return values |
| `plugin.reload` | `name` | Hot-reload a loaded plugin |
| `plugin.unload` | `name` | Hot-unload a loaded plugin |
| `subscribe` | `events`? | Start receiving events; data lists the active subscriptions |
| `quit` | | Terminate the compositor |

`window` is a window `id`. `key` uses the `shady.bind` shortcut syntax and
goes through module hooks, native plugins, Lua bindings and compositor
bindings exactly like a real key press, so any plugin shortcut (`Super+b`
for border-accent, `Super+BackSpace` for rewind) can be triggered from a
script; a key none of them takes goes on to the focused window
([Input](#input)). Plugins can only be loaded while `config.lua` runs, so IPC offers
the same runtime operations as Lua: list, reload and unload (see
[PLUGIN_MANAGER.md](PLUGIN_MANAGER.md)).

## Input

These are for scripts, tests and computer-use agents: a program that looks
at the screen and works it with the keyboard and pointer, as a person does.
Coordinates are layout coordinates, the ones `windows` and `outputs` use,
not pixels of a scaled output.

`key` goes through module hooks, native plugins, Lua bindings and
compositor bindings first, as before (`handled`). A key none of them takes
is then pressed for the focused window (`sent`), with its modifiers, so
`Ctrl+l` reaches a browser and `Return` a terminal. Keys reach windows
through a keyboard of Shady's own, made the first time automation types,
with the configured keymap; it never goes through an input method. A
modifier alone (`"Shift"`) is its own key, which `key.down` can hold over
later commands until `key.up`.

`type` types what is in the keymap as keys on it, Shift included, and the
compositor's handling sees those keys as it would typed ones (a search
field in an overview). Text with anything else, Hangul or emoji for
example, is typed whole with a keymap made for it, as `wtype` does, and
the configured keymap is put back after; keys held with `key.down` are let
go first. `\n` is Return, `\t` Tab.

`button` is `"left"` (the default), `"right"`, `"middle"`, `"side"`,
`"extra"`, `"forward"`, `"back"`, or a Linux button code. A drag over time
lets the window see the press before the pointer moves, as a hand would;
a client that disconnects in the middle of one lets the button go.

```sh
shadyctl pointer.click x=640 y=360
shadyctl type 'text=hello, 세계'
shadyctl key keys=Ctrl+Return
shadyctl pointer.drag x1=100 y1=100 x2=400 y2=300 duration=500
shadyctl wait idle=200 timeout=3000
```

`wait` decides what a change is by `scope`. `"windows"` (the default)
counts the windows' surfaces and their popups, the layout moving windows,
a layer surface while it has the keyboard (a launcher being typed into)
and the lock screen, but not a bar's clock, an animated wallpaper or the
cursor, so a real desktop settles. `window` narrows that to one window
and the layout. `"screen"` counts every frame an output draws. When it does
not settle in time, `active` says what kept changing: `"window 3"`,
`"layout"`, a layer surface's namespace, `"the lock screen"` or `"frames"`.

## Screenshots

`screenshot` captures the next frame an output draws. By default it
answers at once with `{"path": …}`; the value `screenshot.saved` (or
`screenshot.failed`) names the file once it is written. With `"wait": true`
it answers once the file is written, and says how the image maps back to
layout coordinates:

```json
{"path": "/tmp/s.png", "output": "HEADLESS-1", "width": 640, "height": 360,
 "x": 0, "y": 0, "logical_width": 1280, "logical_height": 720, "scale": 0.5}
```

The image pixel `(px, py)` is the layout point `(x + px / scale, y + py / scale)`,
so a click on what an agent saw in the image lands where it saw it.

- `output` names the output; otherwise it is the first to draw.
- `window` (an `id`) or `x`, `y`, `width`, `height` capture only that part,
  on the output under its middle. A window on another workspace is refused.
- `max_width` and `max_height` scale the image down to fit.

One screenshot is pending at a time. Shady draws the 3D scene itself, so a
screenshot shows it as drawn, but a window's box in spatial mode is the
flat one `windows` reports. A rotated output is captured whole, in its
buffer's orientation.

## Events

`subscribe` adds the named events to the connection, or every event when
`events` is absent. It can be sent more than once. Events then arrive as
lines with an `event` field, interleaved with responses:

```json
{"event": "window.focused", "window": {"id": 3, "app_id": "foot", ...}}
{"event": "window.destroyed", "window": {"id": 3}}
{"event": "workspace.changed", "workspace": "two"}
{"event": "output.added", "output": {"name": "DP-1", ...}}
{"event": "module.started", "module": "border-accent"}
```

Event names are the ones `shady.on` uses: `window.created`,
`window.mapped`, `window.unmapped`, `window.focused`, `window.resized`,
`window.state_changed`, `window.title_changed`, `window.destroyed`, `output.added`,
`output.removed`, `workspace.changed`, `module.started`, `module.stopped`.
`window.unmapped` and `window.destroyed` carry only the window `id`.

### Published values

Plugins (`publish_value`) and the compositor's Lua (`shady.publish`) can
publish small named strings, such as a rice's current hour or palette. Read
them with `values` and `value`, and follow them with the `value.changed`
event:

```json
{"event": "value.changed", "key": "afterglow.hour", "value": "blue-hour"}
{"event": "value.changed", "key": "afterglow.hour", "value": null}
```

`null` means the value was removed, for example because the plugin that
published it unloaded. Subscribing to `value.changed` first replays every
current value as an event, so a client needs no separate snapshot.
`value.changed` is included when `subscribe` is sent without `events`.

### Shortcut overlay

`Super+Shift+/`, `shady.toggle_shortcuts()` or the `shortcuts.toggle`
command asks shells to show or hide the overlay. When no client subscribes
to `shortcuts.toggle`, Shady shows its own overlay instead. Subscribers to
`shortcuts.toggle` receive the event with the number of shortcuts, then the
list itself as one `shortcut` event per entry, so a client that only reads
flat objects can collect it:

```json
{"event": "shortcuts.toggle", "count": 2}
{"event": "shortcut", "keys": "Super+Shift+slash", "group": "Shady", "description": "Show these shortcuts"}
{"event": "shortcut", "keys": "Super+B", "group": "border-accent", "description": "Accent the focused window's border"}
```

`keys` is the binding syntax with canonical spelling (`Super+Ctrl+Alt+Shift+Key`),
several keys sharing modifiers as `Alt+Left/Right/Up/Down`, or a free label
such as `W A S D`.

A subscriber that stops reading is disconnected once 4 MiB of output is
queued for it.

## shadyctl

`shadyctl` is the command-line client:

```sh
shadyctl windows
shadyctl window.focus window=3
shadyctl workspace.switch name=two
shadyctl key keys=Super+h
shadyctl subscribe events=window.focused,workspace.changed
shadyctl --raw '{"cmd":"window.maximize","window":3,"state":true}'
```

Arguments are `key=value`; `true`, `false`, `null` and numbers become JSON
values, `events` is split on commas, everything else is a string. It
prints `data` (or the whole response with `--json`) and exits 1 on an
error. After `subscribe` it prints one event per line until the compositor
exits, which makes it easy to feed other tools:

```sh
shadyctl subscribe events=window.focused | while read -r line; do
    jq -r '.window.title' <<<"$line"
done
```

The socket is `-s path`, else `$SHADY_SOCKET`, else
`$XDG_RUNTIME_DIR/shady-$WAYLAND_DISPLAY.sock`.

### Running Lua

```sh
shadyctl lua 'shady.set("window_opacity", 0.9)'
shadyctl lua 'return shady.current_workspace(), shady.workspaces()'
shadyctl lua 'local w = shady.focused_window(); return w and {id = w.id, title = w.title}'
shadyctl lua -f ./script.lua
printf 'return 6 * 7\n' | shadyctl lua -
shadyctl --json lua 'return 42'  # {"id":1,"ok":true,"data":[42]}
```

The CLI reads files locally, relative to its working directory, and sends
their contents. `lua -` (or `lua -f -`) reads standard input. Inline code
must be a single shell argument. The whole JSON request, after escaping,
must fit in 64 KiB. This command requires the `lua` module to be built and
enabled; other IPC commands remain available without it.

The wire request is `{"cmd":"lua","code":"return 42"}`. Optional `name`
sets the Lua chunk name used in errors (default `=shadyctl`). Only Lua text
chunks are accepted. Execution uses the same Lua state as `init.lua`, so
globals, helper functions, bindings, timers and event handlers persist
between commands. The full runtime [Lua API](LUA_API.md) is available.

Return values become a JSON array: `return 42, true, nil` produces
`[42,true,null]`; no return values produce `[]`. Strings, finite numbers,
booleans, nil and plain tables are supported. Consecutive integer keys
starting at 1 become arrays; string keys become objects, including `{}`
for an empty table. Table metamethods are ignored. Return selected fields
from window/output userdata, rather than the userdata itself. Functions,
threads, userdata, sparse or mixed-key tables, non-finite numbers and
cyclic or excessively deep/large tables produce an error. Serialization
is bounded to depth 16, 16,384 values and 1 MiB of JSON, with a conservative
size check for strings.

Syntax, runtime and serialization errors return `ok:false` and make
`shadyctl` exit 1. Source/read/usage errors exit 2. Failed commands do not
roll back changes already made. `print` and `shady.log` write to the
compositor's output; use `return` to send values back to the CLI. Execution
is synchronous on the compositor thread, with the same privileges as
`init.lua`; use it for trusted scripts and schedule long work asynchronously.

## Stability

The protocol version is reported by `version`. New commands, fields and
events may be added within version 1; clients should ignore fields they do
not know. Removing or changing the meaning of anything bumps the version.
