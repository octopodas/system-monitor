#include "monitor.h"
#include <QApplication>
#include <QIcon>
#include <cstring>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <thread>

int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--check") == 0) {
        NvmlReader reader;
        SystemReader systemReader;
        systemReader.read(true);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        const auto system = systemReader.read(false);
        const auto sample = reader.read();
        std::cout << std::fixed << std::setprecision(1);
        if (system.cpuPercent) std::cout << "CPU load: " << *system.cpuPercent << "%\n";
        if (system.memory) std::cout << "System RAM: " << system.memory->used / 1048576 << " / "
                                    << system.memory->total / 1048576 << " MiB\n";
        std::cout << "GPU: " << sample.name << '\n';
        if (sample.gpuPercent) std::cout << "GPU load: " << *sample.gpuPercent << "%\n";
        if (sample.memoryUsed && sample.memoryTotal)
            std::cout << "VRAM: " << *sample.memoryUsed / 1048576 << " / "
                      << *sample.memoryTotal / 1048576 << " MiB\n";
        if (!sample.error.empty()) std::cerr << sample.error << '\n';
        if (system.disk) std::cout << "Root /: " << system.disk->used / 1048576 << " / "
                                  << system.disk->total / 1048576 << " MiB used; "
                                  << system.disk->available / 1048576 << " MiB available; "
                                  << system.disk->reserved / 1048576 << " MiB reserved\n";
        if (!system.error.empty()) std::cerr << system.error << '\n';
        return sample.error.empty() && system.error.empty() && system.cpuPercent ? 0 : 1;
    }
    if (argc == 2 && std::strcmp(argv[1], "--help") == 0) {
        std::cout << "System Monitor — native CPU, RAM, NVIDIA GPU, VRAM and root partition monitor\n"
                     "  system-monitor          Open the desktop window\n"
                     "  system-monitor --check  Print live readings after 1 second (no display needed)\n";
        return 0;
    }
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication app(argc, argv);
    app.setApplicationName("system-monitor");
    app.setApplicationDisplayName("System Monitor");
    app.setOrganizationName("LocalTools");
    app.setDesktopFileName("local-system-monitor");
    app.setWindowIcon(QIcon::fromTheme("local-system-monitor", QIcon::fromTheme("utilities-system-monitor")));
    MonitorWindow window;
    window.show();
    return app.exec();
}
