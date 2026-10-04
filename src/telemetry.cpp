#include "telemetry.h"
#include <dlfcn.h>
#include <utility>
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <limits>
#include <map>

// NVML v2 memory ABI reports allocated and driver-reserved memory separately.
// Match this layout only with nvmlDeviceGetMemoryInfo_v2. No CUDA SDK needed.
namespace {
using Device = void*;
struct Utilization { unsigned gpu, memory; };
struct Memory { unsigned version; unsigned long long total, reserved, free, used; };
// Both v3 running-process entry points use this 24-byte, naturally aligned ABI.
struct ProcessInfo { unsigned pid; unsigned long long used; unsigned gpuInstance, computeInstance; };
using ProcessQuery = int (*)(Device, unsigned*, ProcessInfo*);
}

struct NvmlReader::State {
    std::string library;
    void* module = nullptr;
    Device device = nullptr;
    bool initialized = false;
    std::string name;
    int (*init)() = nullptr;
    int (*shutdown)() = nullptr;
    int (*handle)(unsigned, Device*) = nullptr;
    int (*getName)(Device, char*, unsigned) = nullptr;
    int (*getUtilization)(Device, Utilization*) = nullptr;
    int (*getMemory)(Device, Memory*) = nullptr;
    const char* (*errorString)(int) = nullptr;
    ProcessQuery graphicsProcesses = nullptr;
    ProcessQuery computeProcesses = nullptr;
    int (*processName)(unsigned, char*, unsigned) = nullptr;

    template<typename Function> bool resolve(Function& target, const char* symbol) {
        target = reinterpret_cast<Function>(dlsym(module, symbol));
        return target != nullptr;
    }
    void reset() {
        if (initialized) shutdown();
        initialized = false;
        device = nullptr;
        if (module) dlclose(module);
        module = nullptr;
    }
    std::string error(int code) const {
        const char* text = errorString ? errorString(code) : nullptr;
        return text ? text : "Unknown driver error";
    }
    std::string open() {
        if (device) return {};
        module = dlopen(library.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!module) return "NVIDIA driver library unavailable";
        if (!resolve(init, "nvmlInit_v2") || !resolve(shutdown, "nvmlShutdown") ||
            !resolve(handle, "nvmlDeviceGetHandleByIndex_v2") ||
            !resolve(getName, "nvmlDeviceGetName") ||
            !resolve(getUtilization, "nvmlDeviceGetUtilizationRates") ||
            !resolve(getMemory, "nvmlDeviceGetMemoryInfo_v2") ||
            !resolve(errorString, "nvmlErrorString")) {
            reset();
            return "NVIDIA driver API unavailable";
        }
        int result = init();
        if (result != 0) {
            const auto message = "NVIDIA driver: " + error(result);
            reset();
            return message;
        }
        initialized = true;
        // Optional features must not prevent the main GPU/VRAM readings.
        resolve(graphicsProcesses, "nvmlDeviceGetGraphicsRunningProcesses_v3");
        resolve(computeProcesses, "nvmlDeviceGetComputeRunningProcesses_v3");
        resolve(processName, "nvmlSystemGetProcessName");
        result = handle(0, &device);
        if (result != 0) {
            const auto message = "GPU 0: " + error(result);
            reset();
            return message;
        }
        char buffer[128]{};
        name = getName(device, buffer, sizeof(buffer)) == 0 ? buffer : "NVIDIA GPU 0";
        return {};
    }
    ~State() { reset(); }
};

NvmlReader::NvmlReader(std::string library) : state(std::make_unique<State>()) {
    state->library = std::move(library);
}
NvmlReader::~NvmlReader() = default;

GpuSample NvmlReader::read() {
    GpuSample sample;
    sample.error = state->open();
    sample.name = state->name;
    if (!sample.error.empty()) return sample;

    Utilization utilization{};
    Memory memory{};
    memory.version = sizeof(Memory) | (2u << 24);
    const int gpuResult = state->getUtilization(state->device, &utilization);
    const int memoryResult = state->getMemory(state->device, &memory);
    if (gpuResult == 0 && utilization.gpu <= 100) {
        sample.gpuPercent = utilization.gpu;
    } else {
        sample.error = "GPU load: " + (gpuResult ? state->error(gpuResult) : "Invalid reading");
    }
    if (memoryResult == 0 && memory.total > 0 && memory.used <= memory.total) {
        sample.memoryUsed = memory.used;
        sample.memoryTotal = memory.total;
    } else {
        if (!sample.error.empty()) sample.error += "; ";
        sample.error += "VRAM: " + (memoryResult ? state->error(memoryResult) : "Invalid reading");
    }
    // Reacquire handles after transient errors; NOT_SUPPORTED (3) needs no reset.
    if ((gpuResult != 0 && gpuResult != 3) || (memoryResult != 0 && memoryResult != 3))
        state->reset();
    return sample;
}

GpuProcesses NvmlReader::readProcesses() {
    GpuProcesses result;
    result.error = state->open();
    if (!result.error.empty()) return result;
    std::map<unsigned, GpuProcess> unique;
    auto query = [&](ProcessQuery function, bool graphics) {
        std::string failure;
        if (!function) failure = "API unavailable";
        else {
            unsigned capacity = 32;
            for (int attempt = 0; attempt < 3; ++attempt) {
                std::vector<ProcessInfo> rows(capacity);
                unsigned count = capacity;
                const int code = function(state->device, &count, rows.data());
                if (code == 7) { // INSUFFICIENT_SIZE: processes can start during a query.
                    if (count > 4096 || capacity >= 4096) { failure = "Process list exceeds limit"; break; }
                    capacity = std::min(4096u, std::max(count, capacity * 2));
                    failure = "Process list changed; retrying";
                    continue;
                }
                if (code != 0) { failure = state->error(code); break; }
                if (count > capacity) { failure = "Invalid process count"; break; }
                for (unsigned i = 0; i < count; ++i) {
                    const auto& row = rows[i];
                    auto [entry, inserted] = unique.try_emplace(row.pid, GpuProcess{row.pid, "", false, false, std::nullopt});
                    (void)inserted;
                    auto& process = entry->second;
                    process.graphics |= graphics;
                    process.compute |= !graphics;
                    if (row.used != std::numeric_limits<unsigned long long>::max())
                        process.memoryUsed = std::max(process.memoryUsed.value_or(0), std::uint64_t(row.used));
                }
                failure.clear();
                break;
            }
        }
        if (!failure.empty()) {
            if (!result.error.empty()) result.error += "; ";
            result.error += std::string(graphics ? "Graphics: " : "Compute: ") + failure;
        }
    };
    query(state->graphicsProcesses, true);
    query(state->computeProcesses, false);
    for (auto& [pid, process] : unique) {
        // Prefer executable names: NVML sometimes returns an entire command line.
        std::error_code pathError;
        const auto executable = std::filesystem::read_symlink("/proc/" + std::to_string(pid) + "/exe", pathError);
        if (!pathError) process.name = executable.filename().string();
        else {
            std::ifstream comm("/proc/" + std::to_string(pid) + "/comm");
            std::getline(comm, process.name);
        }
        char name[1024]{};
        if (process.name.empty() && state->processName && state->processName(pid, name, sizeof(name)) == 0) {
            name[sizeof(name) - 1] = '\0';
            process.name = name;
            const auto argument = process.name.find_first_of(" \t\n");
            if (argument != std::string::npos) process.name.erase(argument);
            const auto slash = process.name.find_last_of('/');
            if (slash != std::string::npos) process.name.erase(0, slash + 1);
        }
        if (process.name.empty()) process.name = "PID " + std::to_string(pid);
        result.rows.push_back(std::move(process));
    }
    return result;
}
