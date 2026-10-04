// Test fixture only: implements the small NVML ABI surface used by the app.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>

static int mode = 0;
static int shutdowns = 0;
static std::atomic<int> samples{0};
static std::atomic<int> processQueries{0};
static std::atomic<int> processMode{0};
extern "C" {
void fake_set_mode(int value) { mode = value; }
int fake_shutdown_count() { return shutdowns; }
int fake_sample_count() { return samples.load(); }
int fake_process_queries() { return processQueries.load(); }
void fake_process_mode(int value) { processMode = value; }
struct ProcessInfo { unsigned pid; unsigned long long used; unsigned gpuInstance, computeInstance; };
int processList(bool graphics, unsigned* count, ProcessInfo* rows) {
    ++processQueries;
    if (processMode == 1 && graphics) return 3;
    if (processMode == 2) { *count = 0; return 0; }
    if (processMode == 3) { *count = 5000; return 7; }
    if (!rows || *count < 2) { *count = 2; return 7; }
    if (processMode == 4) { *count += 1; return 7; }
    // Outside Linux's PID range: fixture names cannot collide with host processes.
    rows[0] = {4000000001u, 512ull << 20, 0, 0};
    rows[1] = graphics ? ProcessInfo{4000000002u, ~0ull, 0, 0} : ProcessInfo{4000000003u, 128ull << 20, 0, 0};
    *count = 2;
    return 0;
}
int nvmlDeviceGetGraphicsRunningProcesses_v3(void*, unsigned* count, ProcessInfo* rows) {
    return processList(true, count, rows);
}
int nvmlDeviceGetComputeRunningProcesses_v3(void*, unsigned* count, ProcessInfo* rows) {
    return processList(false, count, rows);
}
int nvmlSystemGetProcessName(unsigned pid, char* text, unsigned length) {
    std::snprintf(text, length, "/test/process-%u", pid);
    return 0;
}
int nvmlInit_v2() { return mode == 1 ? 9 : 0; }
int nvmlShutdown() { ++shutdowns; return 0; }
const char* nvmlErrorString(int code) {
    return code == 3 ? "Not Supported" : "GPU is lost";
}
int nvmlDeviceGetHandleByIndex_v2(unsigned, void** handle) {
    *handle = reinterpret_cast<void*>(1);
    return mode == 5 ? 9 : 0;
}
int nvmlDeviceGetName(void*, char* name, unsigned length) {
    std::snprintf(name, length, "Test NVIDIA GPU");
    return 0;
}
struct Utilization { unsigned gpu, memory; };
int nvmlDeviceGetUtilizationRates(void*, Utilization* value) {
    ++samples;
    if (const char* path = std::getenv("GPU_MONITOR_TEST_SAMPLE_LOG")) {
        if (auto* file = std::fopen(path, "a")) {
            std::fputs("sample\n", file);
            std::fclose(file);
        }
    }
    if (mode == 2 || mode == 6) return mode == 2 ? 3 : 15;
    *value = {mode == 4 ? 150u : 37u, 91u};
    return 0;
}
struct Memory { unsigned long long total, free, used; };
int nvmlDeviceGetMemoryInfo(void*, Memory* value) {
    if (mode == 3 || mode == 6) return 15;
    *value = {8ull << 30, (6ull << 30) - (360ull << 20), (2ull << 30) + (360ull << 20)};
    if (mode == 4) value->total = 0;
    return 0;
}
struct MemoryV2 { unsigned version; unsigned long long total, reserved, free, used; };
int nvmlDeviceGetMemoryInfo_v2(void* device, MemoryV2* value) {
    if (value->version != (sizeof(MemoryV2) | (2u << 24))) return 25;
    Memory legacy{};
    const int result = nvmlDeviceGetMemoryInfo(device, &legacy);
    if (result) return result;
    value->total = legacy.total;
    value->reserved = 360ull << 20;
    value->free = legacy.free;
    value->used = legacy.used - value->reserved;
    return 0;
}
}
