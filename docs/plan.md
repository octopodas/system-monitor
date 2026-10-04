# System Monitor

Native C++17 / Qt 5 application for this Ubuntu 24.04 KDE desktop. Keep the
project in its own `system-monitor` subfolder. Preserve the existing GPU/VRAM
monitor, history, Pause and Always on top controls while adding CPU, RAM and a
root-partition pie chart.

- [x] Read CPU deltas from the first eight aggregate /proc/stat counters, excluding
  duplicate guest time, with idle and iowait treated as idle. Reset on resume.
- [x] Read RAM as MemTotal minus MemAvailable; show used/total GiB and percent.
- [x] Read root (/) capacity with statvfs and render used, available and reserved
  slices; refresh every 15 seconds.
- [x] Keep CPU/RAM/GPU/VRAM at one second using the existing background worker,
  with independent failures and at most 120 points in each of four graphs.
- [x] Rename the executable, title, launcher and project folder to System Monitor;
  use the local-system-monitor desktop identifier to preserve KDE's own app.
- [x] Test counter arithmetic, errors, pause/resume, history bounds, UI layout,
  and native launch; compare real readings and measure resource use.
- [x] Install and open the app, and remove only the verified old GPU Monitor files.

No network access, browser runtime, animations, automatic startup, per-sample
processes, disk sample logs, or new system packages.

## GPU process areas — completed 2026-10-04

- [x] Independent collapsible areas under GPU Load and VRAM.
- [x] Shared bounded NVML graphics/compute query only while expanded; pause stops it.
- [x] PID/name/context listing and VRAM-descending allocation listing.
- [x] Handle duplicates, unavailable memory, disappearing processes and partial failures.
- [x] Automated tests, native live comparison, layout checks, install and Desktop launch.
