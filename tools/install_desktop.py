#!/usr/bin/env python3
"""Create a desktop launcher with an absolute Exec path (no PATH dependency)."""
import pathlib
import sys

prefix = pathlib.Path(sys.argv[1]).expanduser().resolve()
executable = str(prefix / "bin/system-monitor")
if any(character in executable for character in "\n\r"):
    raise SystemExit("Installation path must not contain newlines")
# Two parsing layers: desktop value escaping, then Exec quoting.
quoted = executable.replace("%", "%%")
for character in ("\\", '"', "`", "$"):
    quoted = quoted.replace(character, "\\" + character)
quoted = quoted.replace("\\", "\\\\")
pathlib.Path(sys.argv[2]).write_text(
    "[Desktop Entry]\n"
    "Type=Application\n"
    "Name=System Monitor\n"
    "Comment=Lightweight live CPU, RAM, GPU, VRAM and root partition monitor\n"
    f'Exec="{quoted}"\n'
    "Icon=local-system-monitor\n"
    "Terminal=false\n"
    "Categories=System;Monitor;\n"
    "Keywords=CPU;RAM;GPU;VRAM;Disk;NVIDIA;Performance;\n"
    "StartupWMClass=system-monitor\n",
    encoding="utf-8",
)
