# Taskbar Sync Companion

A separate Plasma 6 widget with a C++ backend that coordinates existing stock Task Managers. It keeps the stock widgets, panel placement, and appearance. Plasma starts the companion with the desktop; no systemd service is used.

The first launch is **observation only**. Add one instance to the desktop, check discovery and UUIDs, then enable **Synchronize taskbars** in its view. The initial order and pinned launchers come from the lowest-ID Task Manager on screen 0. Running-window order lives only in memory and is not restored after logout.

## Build

Requires a Plasma 6 Wayland session, a C++20 compiler, CMake, Ninja, Qt 6 (Core, Gui, Qml, Quick, DBus; Test and Widgets for tests), KDE Frameworks CoreAddons/Config, libplasma, and PlasmaQuick development files. Rebuild after Plasma updates because the existing applet QML item and `tasksModel` are private integration interfaces.

On Arch/CachyOS, install dependencies using your normal package manager:

```sh
sudo pacman -S --needed base-devel cmake ninja extra-cmake-modules qt6-base qt6-declarative kcoreaddons kconfig libplasma plasma-desktop plasma-workspace
cmake -S . -B build -G Ninja -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
ctest --test-dir build --output-on-failure
```

`extra-cmake-modules` is required. CMake fails if it is missing; install it before proceeding.

## Install and inspect

Install from this repository directory:

```sh
sudo cmake --install build
./scripts/add-companion.sh
```

Run the add script as your desktop user. It first backs up `plasma-org.kde.plasma.desktop-appletsrc` to `$XDG_CONFIG_HOME/taskbar-sync-backups` (default `~/.config/taskbar-sync-backups`), then adds one companion to the screen 0 desktop. It leaves panel widgets in place and refuses to add a duplicate. Adding manually through Plasma's widget picker is also supported; back up the configuration first.

The companion lists discovered Task Managers and their compatibility errors. A read-only report includes each applet ID, screen, pinned launcher list, and ordered identities:

```sh
qdbus6 org.kde.plasmashell /TaskbarSync org.freedesktop.DBus.Properties.Get org.local.TaskbarSync report
```

Before enabling synchronization, verify that the report contains all four actual Task Managers and readable `w:` UUID sequences. A `w:` key identifies one window; multiple windows from one app have different UUIDs. A normalized `l:` URL identifies a launcher. This requires a Wayland Plasma session; XWayland applications use KWin's Wayland window UUIDs too. Numeric X11-session window IDs are rejected.

Enable the companion's checkbox only after this discovery check. On later desktop starts, the enabled setting is retained.

## Compatibility and behavior

All participating Task Managers must use manual sorting, disabled grouping, enabled task reordering, and matching visibility and launcher-layout settings. Screen/region filtering is incompatible with one shared sequence. The companion reports incompatible settings without changing them. Desktop/activity identities are compared when their filters are enabled.

Movement signals queue a single reconciliation outside model callbacks. During a drag, peers wait for `dragSource` to clear, then reconcile the final source sequence with `move()`. Keyboard movement works through the same movement notifications. Pin/unpin changes propagate through `launcherList`; `syncLaunchers()` and the stock configuration map persist pins normally.

Window creation, closure, and title changes preserve surviving windows' relative order. Newly discovered or reconnected peers join the shared order. Screen 0 is needed for initial seeding; an already-running session can continue while it is disconnected. Startup placeholders temporarily defer that peer until the window is available.

The backend suppresses its own movement/configuration feedback and guards QObjects with `QPointer`. Unsupported roles, ambiguous identities, or movement failures quarantine the affected model rather than retrying unsupported operations. Recreate the affected widget or restart Plasma after fixing an API failure. Settings incompatibilities recover when the user corrects the settings. Removing the companion stops synchronization immediately.

## Verification

```sh
ctest --test-dir build --output-on-failure
./scripts/run-integration.sh
```

Run integration tests as the desktop user in a Wayland session. They stage package files under `build/test-root` solely as a test fixture and keep test configuration under `build/test-config`. They create stock Task Managers in a separate process; they do not reorder your actual panel widgets. The stock-model harness reads live window UUIDs, moves a window from every source, and verifies pin/unpin convergence. It requires at least two movable tasks. The companion harness checks plugin/QML loading, discovery, observation mode, and duplicate-instance rejection.

The unit suite covers drag completion across four peers, keyboard movement, pin/unpin, window insertion/closure, title changes, adding peers, disconnection/reconnection including screen 0, transient startup tasks, incompatible settings, movement failure quarantine, object destruction, removal, and absence of idle polling. Unit convergence checks use a 250 ms timeout. See [ACCEPTANCE.md](ACCEPTANCE.md) for the live desktop checks that still need a human drag or session restart.

## Remove and recover

Remove the companion through desktop edit mode or run:

```sh
./scripts/remove-companion.sh
python3 scripts/uninstall.py build/install_manifest.txt
sudo python3 scripts/uninstall.py build/install_manifest.txt --apply
```

The second command previews the exact installed files; the last removes only this package's manifest entries. Remove the widget before deleting its files. Stock Task Managers keep their current order and pinned launchers and remain usable.

If panel configuration needs restoration, log out of Plasma, restore the chosen backup to `~/.config/plasma-org.kde.plasma.desktop-appletsrc` (or its configured XDG location), then log in. Restoring while Plasma runs can be overwritten by its in-memory configuration. A loaded C++ plugin may remain mapped until Plasma exits; removing its applet destroys its backend and disconnects synchronization immediately.
