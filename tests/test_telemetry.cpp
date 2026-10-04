#include "telemetry.h"
#include <cassert>
#include <dlfcn.h>
#include <iostream>

int main() {
    void* fixture = dlopen("./build/tests/libnvidia-ml.so.1", RTLD_NOW);
    assert(fixture);
    auto mode = reinterpret_cast<void(*)(int)>(dlsym(fixture, "fake_set_mode"));
    auto shutdowns = reinterpret_cast<int(*)()>(dlsym(fixture, "fake_shutdown_count"));
    auto processMode = reinterpret_cast<void(*)(int)>(dlsym(fixture, "fake_process_mode"));
    {
        NvmlReader reader("./build/tests/libnvidia-ml.so.1");
        auto processes = reader.readProcesses();
        assert(processes.error.empty() && processes.rows.size() == 3);
        assert(processes.rows[0].pid == 4000000001u && processes.rows[0].graphics && processes.rows[0].compute);
        assert(processes.rows[0].name == "process-4000000001");
        assert(processes.rows[0].memoryUsed == (512ull << 20)); // Never sum duplicate contexts.
        assert(!processes.rows[1].memoryUsed); // NVML unavailable sentinel, not a huge allocation.
        processMode(1);
        processes = reader.readProcesses();
        assert(!processes.error.empty() && processes.rows.size() == 2); // Partial data survives.
        processMode(2);
        processes = reader.readProcesses();
        assert(processes.error.empty() && processes.rows.empty());
        for (int failure : {3, 4}) {
            processMode(failure);
            processes = reader.readProcesses();
            assert(!processes.error.empty() && processes.rows.empty());
        }
        processMode(0);
    }
    {
        NvmlReader reader("./build/tests/libnvidia-ml.so.1");
        auto sample = reader.read();
        assert(sample.name == "Test NVIDIA GPU");
        assert(sample.gpuPercent == 37); // memory bandwidth (91%) is NOT VRAM usage.
        assert(sample.memoryUsed == (2ull << 30));
        assert(sample.memoryTotal == (8ull << 30));
        assert(sample.error.empty());

        mode(2); // Unsupported GPU utilization must not erase valid VRAM data.
        sample = reader.read();
        assert(!sample.gpuPercent && sample.memoryUsed && !sample.error.empty());

        mode(3); // Failure must not reuse the previous memory reading.
        sample = reader.read();
        assert(sample.gpuPercent && !sample.memoryUsed && !sample.memoryTotal);

        mode(4); // Invalid driver data must never become a percentage or divide by 0.
        sample = reader.read();
        assert(!sample.gpuPercent && !sample.memoryUsed && !sample.error.empty());

        mode(6);
        sample = reader.read();
        assert(!sample.gpuPercent && !sample.memoryUsed);
        mode(0); // Recover after the device becomes available again.
        sample = reader.read();
        assert(sample.gpuPercent == 37 && sample.error.empty());
    }
    assert(shutdowns() > 0);
    for (int failure : {1, 5}) {
        mode(failure);
        NvmlReader reader("./build/tests/libnvidia-ml.so.1");
        auto sample = reader.read();
        assert(!sample.gpuPercent && !sample.memoryUsed && !sample.error.empty());
        mode(0);
        assert(reader.read().gpuPercent == 37);
    }
    {
        NvmlReader reader("/missing/gpu-monitor-test-library.so");
        auto sample = reader.read();
        assert(!sample.gpuPercent && !sample.error.empty());
    }
    dlclose(fixture);
    std::cout << "Telemetry: success, partial errors, invalid data, recovery, shutdown OK\n";
}
