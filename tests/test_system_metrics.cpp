#include "system_metrics.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>

int main() {
    std::istringstream cpu("cpu 100 20 30 400 50 6 7 8 80 10\ncpu0 0 0 0 0 0 0 0 0\n");
    const auto counters = parseCpuCounters(cpu);
    assert(counters && counters->total == 621 && counters->idle == 450);
    // guest/guest_nice are already included in user/nice, not counted twice.
    assert(cpuUtilization({100, 40}, {200, 115}) == 25.0);
    assert(cpuUtilization({100, 40}, {200, 40}) == 100.0);
    assert(cpuUtilization({100, 40}, {200, 140}) == 0.0);
    assert(!cpuUtilization({100, 40}, {100, 40}));
    assert(!cpuUtilization({100, 40}, {90, 30}));
    assert(!cpuUtilization({100, 40}, {200, 39}));
    assert(!cpuUtilization({100, 40}, {110, 100}));
    std::istringstream invalidCpu("cpu0 100 20\n");
    assert(!parseCpuCounters(invalidCpu));

    std::istringstream memory("MemTotal: 10000 kB\nMemFree: 100 kB\nCached: 4000 kB\nMemAvailable: 6000 kB\n");
    const auto ram = parseMemoryUsage(memory);
    assert(ram && ram->total == 10240000 && ram->used == 4096000);
    for (const char* value : {"MemTotal: 100 kB\n", "MemTotal: 0 kB\nMemAvailable: 0 kB\n",
                              "MemTotal: 10 kB\nMemAvailable: 20 kB\n"}) {
        std::istringstream invalidMemory(value);
        assert(!parseMemoryUsage(invalidMemory));
    }
    const auto disk = diskUsage(1000, 200, 150, 4096);
    assert(disk && disk->total == 4096000 && disk->used == 3276800);
    assert(disk->available == 614400 && disk->reserved == 204800);
    assert(disk->used + disk->available + disk->reserved == disk->total);
    assert(!diskUsage(0, 0, 0, 4096));
    assert(!diskUsage(100, 101, 50, 4096));
    assert(!diskUsage(100, 50, 51, 4096));
    assert(!diskUsage(std::numeric_limits<std::uint64_t>::max(), 0, 0, 4096));

    SystemReader missing("/no-system-monitor-proc", "/no-system-monitor-root");
    auto failed = missing.read(true);
    assert(!failed.cpuPercent && !failed.memory && !failed.disk && !failed.error.empty());
    SystemReader real;
    auto first = real.read(true);
    assert(!first.cpuPercent && first.memory && first.disk && first.error.empty());
    auto second = real.read(false);
    assert(second.disk->total == first.disk->total);
    real.resetCpu();
    assert(!real.read(false).cpuPercent);
    std::cout << "System metrics: CPU deltas, RAM availability, disk reserved space, errors and reset OK\n";
}
