#include "monitor.h"
#include <QApplication>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QTest>
#include <QToolButton>
#include <QTableWidget>
#include <cassert>
#include <dlfcn.h>
#include <iostream>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    void* fixture = dlopen("libnvidia-ml.so.1", RTLD_NOW);
    assert(fixture);
    auto samples = reinterpret_cast<int(*)()>(dlsym(fixture, "fake_sample_count"));
    auto processQueries = reinterpret_cast<int(*)()>(dlsym(fixture, "fake_process_queries"));
    {
        MonitorWindow window;
        window.show();
        QTest::qWait(1250);
        assert(window.windowTitle() == "System Monitor");
        auto* cpu = window.findChild<QLabel*>("cpu-value");
        auto* ram = window.findChild<QLabel*>("ram-value");
        auto* disk = window.findChild<QLabel*>("disk-detail");
        assert(cpu && ram && disk);
        assert(cpu->text().endsWith('%') && ram->text().endsWith('%'));
        assert(disk->text().contains("GiB used"));
        auto* gpu = window.findChild<QLabel*>("gpu-value");
        auto* vram = window.findChild<QLabel*>("vram-value");
        auto* detail = window.findChild<QLabel*>("vram-detail");
        auto* pause = window.findChild<QPushButton*>("pause");
        auto* pin = window.findChild<QCheckBox*>("pin");
        assert(gpu && vram && detail && pause && pin);
        assert(gpu->text() == "37%" && vram->text() == "25%");
        assert(detail->text().contains("2.00 / 8.00 GiB"));
        auto* gpuToggle = window.findChild<QToolButton*>("gpu-processes-toggle");
        auto* vramToggle = window.findChild<QToolButton*>("vram-processes-toggle");
        auto* gpuTable = window.findChild<QTableWidget*>("gpu-processes-table");
        auto* vramTable = window.findChild<QTableWidget*>("vram-processes-table");
        assert(gpuToggle && vramToggle && gpuTable && vramTable);
        assert(!gpuToggle->isChecked() && !vramToggle->isChecked());
        assert(processQueries() == 0);
        gpuToggle->click();
        vramToggle->click();
        QTest::qWait(1200);
        assert(processQueries() == 2); // One shared graphics+compute snapshot, not one per panel.
        assert(gpuTable->rowCount() == 3 && vramTable->rowCount() == 3);
        assert(gpuTable->item(0, 2)->text() == "G + C");
        assert(vramTable->item(0, 2)->text() == "512.0");
        assert(vramTable->item(2, 2)->text() == "—");

        QTest::mouseClick(pause, Qt::LeftButton);
        QTest::qWait(150); // Let an already in-flight sample settle.
        const int pausedCount = samples();
        const int pausedProcesses = processQueries();
        QTest::qWait(3200);
        assert(samples() == pausedCount);
        assert(processQueries() == pausedProcesses);
        assert(pause->text() == "Resume");
        QTest::mouseClick(pause, Qt::LeftButton);
        QTest::qWait(200);
        assert(samples() > pausedCount);
        gpuToggle->click();
        vramToggle->click();
        QTest::qWait(100);
        const int closedProcesses = processQueries();
        QTest::qWait(3200);
        assert(processQueries() == closedProcesses);

        pin->click();
        assert(window.windowFlags().testFlag(Qt::WindowStaysOnTopHint));
        pin->click();
        assert(!window.windowFlags().testFlag(Qt::WindowStaysOnTopHint));

        MonitorSample failed;
        failed.gpu.error = "Test driver disconnected";
        failed.system.error = "Test system readings unavailable";
        window.showSample(failed);
        assert(gpu->text() == "—" && vram->text() == "—");
        assert(detail->text() == "Unavailable");
        assert(cpu->text() == "—" && ram->text() == "—");
        assert(disk->text() == "Unavailable");
        assert(window.findChild<QLabel*>("status")->text().contains("unavailable"));
        window.resize(window.minimumSize());
        QTest::qWait(50);
        assert(window.grab().save("build/tests/ui-unavailable.png"));
        for (auto* widget : window.findChildren<QWidget*>()) {
            if (auto* chart = dynamic_cast<Chart*>(widget))
                assert(chart->geometry().bottom() < chart->parentWidget()->height());
        }
    } // Destruction must stop/join the worker without a running-QThread crash.
    Chart chart(QColor("#1685dd"));
    for (int i = 0; i < 1000; ++i) chart.append(i % 101, i * 1000);
    assert(chart.sampleCount() <= 120);
    chart.append(std::nullopt, 1000000);
    assert(chart.sampleCount() <= 120);
    chart.append(10.0, 1200001); // A long suspend must expire pre-suspend history.
    assert(chart.sampleCount() == 1);
    dlclose(fixture);
    std::cout << "UI: values, errors, pause/resume, pin, bounded history, shutdown OK\n";
}
