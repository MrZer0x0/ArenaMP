#!/usr/bin/env bash
set -euo pipefail

# Portable release entry point. No root privileges or system-wide changes.
gamedir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
launcher="$gamedir/arenamp-launcher"
if [[ ! -f "$launcher" ]]; then
    printf 'ArenaMP launcher not found: %s\n' "$launcher" >&2
    exit 1
fi
if [[ ! -x "$launcher" ]]; then chmod u+x -- "$launcher"; fi

# Desktop Entry strings and Exec arguments have different escaping rules.
desktop_string() {
    local value="$1"
    value="${value//\\/\\\\}"
    value="${value//$'\n'/\\n}"
    value="${value//$'\r'/\\r}"
    printf '%s' "$value"
}
exec_argument() {
    local value="$1"
    value="${value//\\/\\\\}"
    value="${value//\"/\\\"}"
    value="${value//\$/\\\$}"
    value="${value//\`/\\\`}"
    value="${value//%/%%}"
    desktop_string "\"$value\""
}
install_shortcut() {
    local applications="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
    local desktop=""
    if command -v xdg-user-dir >/dev/null 2>&1; then
        desktop="$(xdg-user-dir DESKTOP 2>/dev/null || true)"
    elif [[ -d "$HOME/Desktop" ]]; then
        desktop="$HOME/Desktop"
    fi
    mkdir -p -- "$applications" || return 1
    local entry="$applications/arenamp-portable.desktop"
    {
        printf '[Desktop Entry]\nType=Application\nVersion=1.0\nName=ArenaMP\n'
        printf 'Comment=ArenaMP launcher\nTerminal=false\nCategories=Game;RolePlaying;\n'
        # bash also works when a ZIP extractor discarded the script's +x bit.
        printf 'Exec=/bin/bash %s --no-shortcut\n' "$(exec_argument "$gamedir/arena_mp_start.sh")"
        printf 'Path=%s\n' "$(desktop_string "$gamedir")"
        printf 'Icon=%s\n' "$(desktop_string "$gamedir/arena_mp_icon.png")"
    } > "$entry" || return 1
    chmod u+x -- "$entry"
    if [[ -n "$desktop" && "$desktop" != "$HOME" && -d "$desktop" ]]; then
        cp -- "$entry" "$desktop/arenamp-portable.desktop" || return 1
        chmod u+x -- "$desktop/arenamp-portable.desktop"
        if command -v gio >/dev/null 2>&1; then
            gio set "$desktop/arenamp-portable.desktop" metadata::trusted true 2>/dev/null || true
        fi
    fi
}
case "${1:-}" in
    --no-shortcut) shift ;;
    --install-shortcut) install_shortcut; exit ;;
    *) install_shortcut || printf 'Could not install ArenaMP shortcut. Starting launcher.\n' >&2 ;;
esac
cd -- "$gamedir"

# The portable binaries carry relative RUNPATH entries and qt.conf points Qt at
# the matching bundled plugin tree. Do not export LD_LIBRARY_PATH here: host
# utilities started by the launcher must keep the host Mesa/OpenSSL/C++ runtime.
# Y079: use XCB/Xwayland by default even when the desktop session itself is
# Wayland. The bundled portable runtime deliberately does not ship a partial
# Qt Wayland integration stack.
export QT_QPA_PLATFORM="${ARENAMP_QT_PLATFORM:-xcb}"
unset QT_QPA_PLATFORMTHEME QT_STYLE_OVERRIDE
exec "$launcher" "$@"
