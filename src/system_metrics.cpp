#include "system_metrics.h"
#include <fstream>
#include <limits>
#include <sstream>
#include <sys/statvfs.h>
#include <utility>

std::optional<CpuCounters> parseCpuCounters(std::istream& input) {
    std::string line, name;
    std::getline(input, line);
    std::istringstream fields(line);
    fields >> name;
    if (name != "cpu") return std::nullopt;
    std::uint64_t counters[8]{};
    CpuCounters result{};
    for (auto& value : counters) {
        if (!(fields >> value) || value > std::numeric_limits<std::uint64_t>::max() - result.total)
            return std::nullopt;
        result.total += value;
    }
    // guest and guest_nice (fields 9/10) are already included in user/nice.
    result.idle = counters[3] + counters[4];
    return result;
}

std::optional<double> cpuUtilization(CpuCounters before, CpuCounters after) {
    if (after.total <= before.total || after.idle < before.idle) return std::nullopt;
    const auto elapsed = after.total - before.total;
    const auto idle = after.idle - before.idle;
    if (idle > elapsed) return std::nullopt;
    return 100.0 * (elapsed - idle) / elapsed;
}

std::optional<MemoryUsage> parseMemoryUsage(std::istream& input) {
    std::optional<std::uint64_t> total, available;
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream fields(line);
        std::string name, unit;
        std::uint64_t value;
        if (!(fields >> name >> value >> unit) || unit != "kB") continue;
        if (value > std::numeric_limits<std::uint64_t>::max() / 1024) return std::nullopt;
        if (name == "MemTotal:") total = value * 1024;
        if (name == "MemAvailable:") available = value * 1024;
        if (total && available) break;
    }
    if (!total || !available || !*total || *available > *total) return std::nullopt;
    return MemoryUsage{*total - *available, *total};
}

std::optional<DiskUsage> diskUsage(std::uint64_t blocks, std::uint64_t freeBlocks,
                                  std::uint64_t availableBlocks, std::uint64_t blockSize) {
    if (!blocks || !blockSize || freeBlocks > blocks || availableBlocks > freeBlocks ||
        blocks > std::numeric_limits<std::uint64_t>::max() / blockSize) return std::nullopt;
    return DiskUsage{(blocks - freeBlocks) * blockSize, availableBlocks * blockSize,
                     (freeBlocks - availableBlocks) * blockSize, blocks * blockSize};
}

SystemReader::SystemReader(std::string proc, std::string root)
    : procRoot(std::move(proc)), partition(std::move(root)) {}

SystemSample SystemReader::read(bool refreshDisk) {
    SystemSample sample;
    auto error = [&sample](const std::string& message) {
        if (!sample.error.empty()) sample.error += "; ";
        sample.error += message;
    };
    std::ifstream stat(procRoot + "/stat");
    const auto currentCpu = parseCpuCounters(stat);
    if (currentCpu && previousCpu) {
        sample.cpuPercent = cpuUtilization(*previousCpu, *currentCpu);
        if (!sample.cpuPercent) error("CPU counters changed; resampling");
    } else if (!currentCpu) error("CPU counters unavailable");
    previousCpu = currentCpu;

    std::ifstream meminfo(procRoot + "/meminfo");
    sample.memory = parseMemoryUsage(meminfo);
    if (!sample.memory) error("System RAM unavailable");

    if (refreshDisk) {
        struct statvfs info{};
        disk.reset();
        diskError.clear();
        if (statvfs(partition.c_str(), &info) == 0)
            disk = diskUsage(info.f_blocks, info.f_bfree, info.f_bavail, info.f_frsize);
        if (!disk) diskError = "Root partition unavailable";
    }
    sample.disk = disk;
    if (!diskError.empty()) error(diskError);
    return sample;
}
