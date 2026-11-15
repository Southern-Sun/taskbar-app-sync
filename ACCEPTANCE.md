# Acceptance checks

Automated checks were run on Plasma 6.8.2 with Qt 6.11. Stock-model integration uses separate instances reading live Wayland window data. The user subsequently reported that all manual tests passed except the logout/login check, which remains untested. Automated timings do not independently establish physical drop latency.

| Check | Evidence/status |
| --- | --- |
| Build plugin with installed ECM | Passed |
| Unit suite: four peers, final drop, pins, keyboard, lifetimes, membership, settings, reconnect, idle | Passed |
| Four stock QML models: readable live UUIDs, moves from each source, pin/unpin | Passed |
| Companion package/C++ plugin/QML load, discovers four stock roots, observation, duplicate rejection | Passed |
| Inventory of actual panels | Four stock taskmanager widgets previously found on screens 0–3 |
| Companion discovery of the four actual panel UUID sequences | Passed: widgets 45, 145, 121, 169 on screens 0, 1, 2, 3; no compatibility errors |
| Initial convergence of actual panels after enable | Passed: all four report the same 23-task sequence seeded from screen 0 |
| Drag windows from each actual monitor, including multiple windows of one app | Passed (user reported) |
| Native Wayland and XWayland windows individually identified and reordered | Passed (user reported) |
| Actual taskbar pin/unpin persistence and keyboard reorder | Passed (user reported) |
| Actual window create/close and title changes | Passed (user reported) |
| Monitor reconnection, Plasma restart, removal and incompatible settings | Passed (user reported) |
| Logout/login: companion reloads, synchronization resumes, pins persist | Pending |
| Actual drop-to-convergence latency ≤250 ms without oscillation | Passed (user reported) |

After installation, capture the read-only report before enabling. Confirm four widget IDs and screen numbers and inspect each `w:` sequence. Resolve any compatibility errors in the stock widgets yourself; the companion will not silently change settings.

Enable synchronization, then drag an individual window from every monitor. Verify identical orders after each drop, including several windows of one application. Repeat with keyboard reordering, native Wayland and XWayland applications. Observe for several seconds after every drop to check for oscillation.

Pin and unpin on each monitor, change launcher order, open and close windows, and change titles. Surviving windows should retain relative order. Confirm pinned launchers survive a Plasma restart. Running-window order need not survive a full logout.

Disconnect/reconnect a monitor; verify remaining peers still work and the returning widget joins the shared order. Disable grouping/manual sorting on one widget to confirm an explicit compatibility message, then correct it and check recovery. Remove the companion and verify the stock widgets operate independently.

Measure drop-to-last-peer convergence using timestamped screen recording or GUI instrumentation; the 20 ms coalescing timer alone does not establish real desktop latency.

Live installation check: companion 170 was added to the screen 0 desktop and enabled after observation. Panel configuration was backed up to `/home/john/.config/taskbar-sync-backups/plasma-org.kde.plasma.desktop-appletsrc.20261115-134018-652192033`. The initial live report is saved under the ignored build directory as `build/live-panel-report.json`. The user reported that manual drag/drop, restart, and physical monitor checks passed. Logout/login remains pending.
