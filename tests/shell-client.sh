# Sourced by the tests that use shady-shell as their layer-shell client.
# shady-shell is its own project (https://github.com/gloaming-dusk/shady-shell);
# these tests take it from $SHADY_SHELL_BIN, the build directory, or PATH,
# and are skipped (status 77) without one.
shell_bin="${SHADY_SHELL_BIN:-}"
if [[ -z "$shell_bin" && -x "$BUILD_DIR/shady-shell" ]]; then
    shell_bin="$(cd "$BUILD_DIR" && pwd)/shady-shell"
fi
[[ -n "$shell_bin" ]] || shell_bin="$(command -v shady-shell || true)"
if [[ ! -x "$shell_bin" ]]; then
    echo "$(basename "$0" .sh): skipped, needs shady-shell (SHADY_SHELL_BIN, or on PATH)" >&2
    exit 77
fi
export SHADY_SHELL_BIN="$shell_bin"
