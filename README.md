# System Monitor

A lightweight native C++/Qt desktop window for CPU, system RAM, NVIDIA GPU,
VRAM, and root-partition usage. Built and tested on Ubuntu 24.04 / KDE with an
NVIDIA RTX 3060 Ti.

## Run

Open **System Monitor** from the application menu, or run:

```sh
~/.local/bin/system-monitor
```

- **CPU load:** busy time across all cores, sampled once per second. The first
  percentage appears after one sampling interval; it is not the load average.
- **System RAM:** total minus Linux `MemAvailable`, so reclaimable cache counts
  as available. Displays percentage and used/total GiB.
- **GPU load:** GPU busy time, with allocated/total VRAM shown separately. NVML
  v2 matches the `nvidia-smi` used-memory field, excluding driver-reserved VRAM.
- **Processes:** expand the areas beneath GPU Load or VRAM. GPU Load lists
  executable names, PIDs, and graphics (G)/compute (C) contexts; these contexts
  may be idle and are not per-process activity percentages. VRAM lists the same
  processes by allocation, largest first, in MiB. Hover a shortened name to see
  it in full. Unavailable memory is shown as a dash. Driver overhead and shared
  allocations mean process memory may not sum to the overall VRAM reading.
- Both process areas start collapsed and share one driver query every three
  sampling ticks while either is open. Collapsing both or pausing stops those
  queries. Lists scroll independently; the whole window can scroll when expanded.
- **Root partition (/):** pie chart for used, available, and filesystem-reserved
  space, refreshed every 15 seconds. Its percentage uses the whole partition
  capacity; `df` uses used plus available, excluding reserved space, so `df`'s
  percentage can be higher.
- Four bounded history graphs show the last two minutes on a 0–100% scale.
- **Always on top** pins the window. **Pause** stops all polling; **Resume**
  clears history and resets the CPU baseline for a fresh interval.
- Closing the window stops the app. Nothing runs as a background service.

## Build, test, install

Required dependencies: `g++`, `make`, `pkg-config`, `qtbase5-dev`, Qt 5
Widgets, and a working NVIDIA driver with NVML v2 memory support. No CUDA SDK,
browser, or pip/npm packages are needed. Python 3 only generates the launcher
at install time; the application is a native executable.

On Ubuntu or Debian, install the build dependencies with:

```sh
sudo apt install g++ make pkg-config qtbase5-dev python3
```

Clone the project and build it:

```sh
git clone https://github.com/octopodas/system-monitor.git
cd system-monitor
make -j2
make test
make install
```

Run `./build/system-monitor` before installation. `make install` writes the
binary, icon and launcher under `~/.local`, without sudo. Its desktop identifier
is `local-system-monitor`, so it does not overwrite KDE's own monitor.
`make uninstall` removes those three installed files; `make clean` removes build
outputs.

## Efficiency and errors

One low-priority worker reads `/proc/stat`, `/proc/meminfo`, and NVML once per
second. `statvfs("/")` runs every 15 seconds. No per-sample subprocesses,
animations, sample logs, network access, or unbounded history buffers are used.
The graphs retain at most 120 points each. The partition pie repaints only when
its values change or the window needs repainting. Linux suspend time expires old
history correctly. Sampling continues while minimized; hidden widgets do not
need painting.

Metrics fail independently: GPU trouble does not stop CPU/RAM updates. Failed
readings are shown as unavailable instead of stale values. A wholly unavailable
GPU retries every five seconds; partial readings continue at one second. Only
NVIDIA GPU index 0 is supported; CPU, RAM and disk do not depend on NVIDIA.

## Verify live readings

```sh
./build/system-monitor --check
free -m
df -BM /
nvidia-smi --query-gpu=utilization.gpu,memory.used,memory.total --format=csv
```

The check waits one second to measure CPU usage. Sequential readings can differ
as workloads change; integer MiB display rounding can also differ by one MiB.

Source boundaries: `src/system_metrics.cpp` reads Linux counters,
`src/telemetry.cpp` reads NVML, and `src/monitor.cpp` renders the window and owns
the polling worker. Automated GPU tests use an isolated fake driver, never
fallback data in the installed app.

References: [Linux proc counters](https://docs.kernel.org/filesystems/proc.html),
[statvfs](https://man7.org/linux/man-pages/man3/statvfs.3.html),
[NVIDIA NVML](https://docs.nvidia.com/deploy/nvml-api/index.html), and
[Qt Widgets](https://doc.qt.io/archives/qt-5.15/qtwidgets-index.html).
