#pragma once
#include <cstdint>
#include <istream>
#include <optional>
#include <string>

struct CpuCounters { std::uint64_t total, idle; };
struct MemoryUsage { std::uint64_t used, total; };
struct DiskUsage { std::uint64_t used, available, reserved, total; };
struct SystemSample {
    std::optional<double> cpuPercent;
    std::optional<MemoryUsage> memory;
    std::optional<DiskUsage> disk;
    std::string error;
};

std::optional<CpuCounters> parseCpuCounters(std::istream& input);
std::optional<double> cpuUtilization(CpuCounters before, CpuCounters after);
std::optional<MemoryUsage> parseMemoryUsage(std::istream& input);
std::optional<DiskUsage> diskUsage(std::uint64_t blocks, std::uint64_t freeBlocks,
                                  std::uint64_t availableBlocks, std::uint64_t blockSize);

class SystemReader {
public:
    explicit SystemReader(std::string procRoot = "/proc", std::string partition = "/");
    SystemSample read(bool refreshDisk);
    void resetCpu() { previousCpu.reset(); }
private:
    std::string procRoot;
    std::string partition;
    std::optional<CpuCounters> previousCpu;
    std::optional<DiskUsage> disk;
    std::string diskError;
};
