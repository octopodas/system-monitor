#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct GpuProcess {
    unsigned pid;
    std::string name;
    bool graphics = false;
    bool compute = false;
    std::optional<std::uint64_t> memoryUsed;
};
struct GpuProcesses {
    std::vector<GpuProcess> rows;
    std::string error;
};

struct GpuSample {
    std::string name;
    std::optional<unsigned> gpuPercent;
    std::optional<std::uint64_t> memoryUsed;
    std::optional<std::uint64_t> memoryTotal;
    std::string error;
};

// Driver calls (read and destruction after read) belong to the polling thread.
class NvmlReader {
public:
    explicit NvmlReader(std::string library = "libnvidia-ml.so.1");
    ~NvmlReader();
    GpuSample read();
    GpuProcesses readProcesses();
private:
    struct State;
    std::unique_ptr<State> state;
};
