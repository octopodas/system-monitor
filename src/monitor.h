#pragma once
#include "telemetry.h"
#include "system_metrics.h"
#include <QThread>
#include <QTimer>
#include <QWidget>
#include <deque>

class QLabel;
class QPushButton;
class QCheckBox;
class QToolButton;
class QTableWidget;
struct MonitorSample {
    GpuSample gpu;
    SystemSample system;
    std::optional<GpuProcesses> processes;
};
Q_DECLARE_METATYPE(MonitorSample)

class Poller : public QObject {
    Q_OBJECT
public:
    Poller();
public slots:
    void start();
    void setPaused(bool paused);
    void setProcessesEnabled(bool enabled);
signals:
    void sampled(MonitorSample sample);
private:
    void poll();
    QTimer* timer;
    NvmlReader reader;
    SystemReader systemReader;
    GpuSample gpu;
    qint64 nextGpuRetry = 0;
    qint64 nextDiskRead = 0;
    unsigned processTicksRemaining = 0;
    bool processesEnabled = false;
};

class ProcessPanel : public QWidget {
public:
    ProcessPanel(bool memory, const QString& name, QWidget* parent = nullptr);
    void showProcesses(const GpuProcesses& processes);
    QToolButton* toggle;
private:
    bool memory;
    QWidget* body;
    QTableWidget* table;
    QLabel* message;
};

class Chart : public QWidget {
public:
    explicit Chart(QColor accent, QWidget* parent = nullptr);
    void append(std::optional<double> percent, qint64 milliseconds);
    void clear();
    std::size_t sampleCount() const { return history.size(); }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    struct Point { qint64 time; std::optional<double> value; };
    std::deque<Point> history;
    QColor accent;
};

class DiskPie : public QWidget {
public:
    explicit DiskPie(QWidget* parent = nullptr);
    void setUsage(std::optional<DiskUsage> usage);
protected:
    void paintEvent(QPaintEvent*) override;
private:
    std::optional<DiskUsage> usage;
};

class MonitorWindow : public QWidget {
public:
    MonitorWindow();
    ~MonitorWindow() override;
    void showSample(const MonitorSample& sample);
private:
    QLabel* device;
    QLabel* cpuValue;
    QLabel* ramValue;
    QLabel* ramDetail;
    QLabel* gpuValue;
    QLabel* vramValue;
    QLabel* vramDetail;
    QLabel* status;
    QLabel* error;
    QLabel* diskValue;
    QLabel* diskDetail;
    QLabel* diskLegend;
    DiskPie* diskPie;
    Chart* cpuChart;
    Chart* ramChart;
    Chart* gpuChart;
    Chart* vramChart;
    ProcessPanel* gpuProcesses;
    ProcessPanel* vramProcesses;
    QPushButton* pause;
    QCheckBox* pin;
    QThread worker;
    Poller* poller;
    bool paused = false;
};
