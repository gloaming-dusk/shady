# Greeter

The Shady greeter is the login screen [greetd](https://sr.ht/~kennylevinsen/greetd/)
shows before anyone logs in: Shady itself, running the Afterglow sky and sea,
with a login card from `shady-shell` in front of it. Logging in carries on
into a Shady session under the same sky.

## Using it

On NixOS, with the flake's module:

```nix
programs.shady.enable = true;
programs.shady.greeter.enable = true;
```

This enables greetd with `shady-greeter` as its default session, run as the
`greeter` user, and keeps the last user and session in
`/var/lib/shady-greeter/last`.

Elsewhere, point greetd at the installed script:

```toml
[default_session]
command = "shady-greeter"
user = "greeter"
```

Type the password and press Enter. Up and Down pick the user, Tab the
session, Escape clears the field. Restart and Shut down run
`systemctl reboot` / `systemctl poweroff`.

## Pieces

| | |
|---|---|
| `shady-greeter` (`data/shady-greeter.in`) | starts Shady with the greeter's config, `shady-shell` as its only client, and logs to the journal (`journalctl -t shady-greeter`) |
| `greeter/config.lua` | the compositor: Afterglow's sky plugin and no shortcuts at all, so nothing on the login screen can start programs (even the default Escape-to-quit is unbound) |
| `greeter/init.lua` | frosted glass behind the card and Afterglow's camera |
| `greeter/shell.lua` | the login card: a `persistent` popup with exclusive keyboard focus |
| `src/greeter/greetd.c` | the `greetd` shell plugin: speaks greetd's IPC and lists accounts and sessions |

The card asks the plugin to authenticate; when greetd accepts the chosen
session the plugin asks the compositor to quit over its IPC socket, and
greetd starts the session once the greeter has exited.

## The greetd plugin

Values (read with `shell.value`):

| Value | |
|---|---|
| `greetd.users` | one `name\tdisplay name` line per regular account (UID 1000–59999 with a login shell) |
| `greetd.sessions` | one `name\tid` line per `.desktop` file in the session directories |
| `greetd.state` | `idle`, `busy`, `prompt`, `done` or `error` |
| `greetd.prompt`, `greetd.secret` | PAM's question while in `prompt`, and `"1"` when the answer should be hidden |
| `greetd.message`, `greetd.message_kind` | the last info or error text (`info` / `error`) |
| `greetd.last_user`, `greetd.last_session` | the previous login |

Actions (run with `shell.action`):

| Action | |
|---|---|
| `greetd.session(id)` | the session to start once authenticated |
| `greetd.login(user)` | start authenticating `user` |
| `greetd.respond(text)` | answer PAM; an answer given before the question arrives is kept for it, so `login` and `respond` together need a single Enter |
| `greetd.cancel()` | give up the current attempt |

Sessions come from the colon-separated directories in
`SHADY_GREETER_SESSIONS` (by default `/run/current-system/sw/share/wayland-sessions`,
`/usr/local/share/wayland-sessions` and `/usr/share/wayland-sessions`) and
are started as `sh -c "<Exec>"`. The last login is stored in
`SHADY_GREETER_STATE`, or `$XDG_STATE_HOME/shady-greeter/last`.

## Trying it

`SHADY_GREETER_DEMO=1` replaces greetd with a fake that accepts any
non-empty password, so the greeter can run nested or headless:

```sh
SHADY_GREETER_DEMO=1 WLR_BACKENDS=wayland shady-greeter
```

`nix build .#checks.x86_64-linux.greeter` boots a VM to the greeter, fails a
login, logs in and checks that the user's Shady session starts.
