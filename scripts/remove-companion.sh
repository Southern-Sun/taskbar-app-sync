#!/usr/bin/env bash
set -euo pipefail
qdbus6 org.kde.plasmashell /PlasmaShell org.kde.PlasmaShell.evaluateScript '
for (var c of desktops().concat(panels())) {
    for (var w of c.widgets()) if (w.type === "org.local.taskbar.sync") w.remove();
}
print("Companion removed; stock Task Managers remain usable.");'
