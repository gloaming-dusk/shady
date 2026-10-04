# IPC API

Shady listens on a Unix socket so that shells, bars and scripts can read
compositor state, send commands and follow events without being Lua or a
native plugin. It is the foundation for the scriptable shell described in
[SHELL_DESIGN.md](SHELL_DESIGN.md), and it is equally usable from any
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

Every request gets exactly one response line, in order:

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

## Commands

| Command | Arguments | Effect |
|---|---|---|
| `window.focus` | `window` | Focus the window, switching to its workspace first |
| `window.close` | `window` | Ask the client to close |
| `window.maximize` | `window`, `state`? | Set maximized; toggles when `state` is absent |
| `window.fullscreen` | `window`, `state`? | Set fullscreen; toggles when `state` is absent |
| `window.move` | `window`, `workspace` | Move to a workspace, creating it if needed |
| `workspace.switch` | `name` | Switch workspace, creating it if needed |
| `key` | `keys` | Press and release a shortcut such as `"Super+Shift+z"`; data is `{"handled": bool}` |
| `plugin.reload` | `name` | Hot-reload a loaded plugin |
| `plugin.unload` | `name` | Hot-unload a loaded plugin |
| `subscribe` | `events`? | Start receiving events; data lists the active subscriptions |
| `quit` | | Terminate the compositor |

`window` is a window `id`. `key` uses the `shady.bind` shortcut syntax and
goes through module hooks, native plugins, Lua bindings and compositor
bindings exactly like a real key press, so any plugin shortcut (`Super+h`
for black-hole, `Super+z` for frozen-window) can be triggered from a
script. Plugins can only be loaded while `config.lua` runs, so IPC offers
the same runtime operations as Lua: list, reload and unload (see
[PLUGIN_MANAGER.md](PLUGIN_MANAGER.md)).

## Events

`subscribe` adds the named events to the connection, or every event when
`events` is absent. It can be sent more than once. Events then arrive as
lines with an `event` field, interleaved with responses:

```json
{"event": "window.focused", "window": {"id": 3, "app_id": "foot", ...}}
{"event": "window.destroyed", "window": {"id": 3}}
{"event": "workspace.changed", "workspace": "two"}
{"event": "output.added", "output": {"name": "DP-1", ...}}
{"event": "module.started", "module": "frozen-window"}
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

## Stability

The protocol version is reported by `version`. New commands, fields and
events may be added within version 1; clients should ignore fields they do
not know. Removing or changing the meaning of anything bumps the version.
