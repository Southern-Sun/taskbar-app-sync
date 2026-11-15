#!/usr/bin/env python3
"""Remove exactly this package's files from a CMake install manifest."""
import argparse
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('manifest', type=Path)
parser.add_argument('--apply', action='store_true', help='Remove files; default prints a preview')
args = parser.parse_args()
paths = [Path(line) for line in args.manifest.read_text().splitlines() if line]
# Refuse manifests for other projects, including unexpected suffixes.
for path in paths:
    package_path = '/share/plasma/plasmoids/org.local.taskbar.sync/' in str(path)
    plugin_path = str(path).endswith('/plugins/plasma/applets/org.local.taskbar.sync.so')
    if not path.is_absolute() or '..' in path.parts or not (package_path or plugin_path):
        parser.error(f'Unexpected manifest entry: {path}')
for path in paths:
    print(('Removing ' if args.apply else 'Would remove ') + str(path))
    if args.apply:
        path.unlink(missing_ok=True)
# Empty directories can stay; never recursively remove anything from a manifest.
