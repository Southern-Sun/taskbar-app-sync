#!/usr/bin/env bash
set -euo pipefail
# Run as the desktop user after system installation, never with sudo.
if [[ $EUID == 0 ]]; then echo 'Run this as your desktop user, without sudo.' >&2; exit 1; fi
config_dir=${XDG_CONFIG_HOME:-"$HOME/.config"}
backup_dir="$config_dir/taskbar-sync-backups"
mkdir -p -- "$backup_dir"
chmod 700 -- "$backup_dir"
backup_path="$backup_dir/plasma-org.kde.plasma.desktop-appletsrc.$(date +%Y%m%d-%H%M%S-%N)"
cp -p -- "$config_dir/plasma-org.kde.plasma.desktop-appletsrc" "$backup_path"
printf 'Panel configuration backed up to %s\n' "$backup_path"
qdbus6 org.kde.plasmashell /PlasmaShell org.kde.PlasmaShell.evaluateScript '
var id = "org.local.taskbar.sync";
var containers = desktops().concat(panels());
var existing = [];
for (var c of containers) for (var w of c.widgets()) if (w.type === id) existing.push(w.id);
if (existing.length > 0) { print("Companion already exists: " + existing.join(", ")); }
else {
    var desktop = desktops().find(d => d.screen === 0);
    if (!desktop) throw new Error("Screen 0 desktop is unavailable");
    var widget = desktop.addWidget(id);
    print("Added companion " + widget.id + " in observation mode. Check its status before enabling synchronization.");
}'
