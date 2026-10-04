#include "monitor.h"
#include <QCheckBox>
#include <QFrame>
#include <QFontMetricsF>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>
#include <QToolButton>
#include <QTableWidget>
#include <QHeaderView>
#include <QScrollArea>
#include <QScrollBar>
#include <QScreen>
#include <QGuiApplication>
#include <algorithm>
#include <time.h>

namespace {
constexpr qint64 HistoryMs = 120000;
const QColor GpuColor("#438fde");
const QColor VramColor("#23a99b");
const QColor CpuColor("#a479d8");
const QColor RamColor("#d9972b");
const QColor ReservedColor("#9699a4");
constexpr double GiB = 1024.0 * 1024.0 * 1024.0;
const char* CardStyle = "QFrame#metric { background: palette(base); border: 1px solid palette(mid); border-radius: 10px; }";

qint64 bootMilliseconds() {
    timespec now{};
    clock_gettime(CLOCK_BOOTTIME, &now);
    return qint64(now.tv_sec) * 1000 + now.tv_nsec / 1000000;
}

QLabel* label(const QString& text, int points, bool bold = false) {
    auto* result = new QLabel(text);
    QFont font = result->font();
    font.setPointSize(points);
    font.setBold(bold);
    result->setFont(font);
    return result;
}

QWidget* metricCard(const QString& title, const QColor& color, QLabel*& value,
                QLabel*& detail, Chart*& chart, QWidget* parent) {
    auto* card = new QFrame(parent);
    card->setObjectName("metric");
    card->setStyleSheet(CardStyle);
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 12, 16, 10);
    layout->setSpacing(4);
    auto* heading = new QHBoxLayout;
    auto* titleLabel = label(title, 10, true);
    titleLabel->setStyleSheet("color: " + color.name());
    heading->addWidget(titleLabel);
    heading->addStretch();
    value = label("—", 24, true);
    heading->addWidget(value);
    layout->addLayout(heading);
    detail = label("Waiting for reading…", 9);
    layout->addWidget(detail);
    chart = new Chart(color, card);
    layout->addWidget(chart, 1);
    return card;
}

std::optional<double> showMemory(const std::optional<MemoryUsage>& memory, QLabel* value, QLabel* detail) {
    if (!memory || !memory->total || memory->used > memory->total) {
        value->setText("—");
        detail->setText("Unavailable");
        return std::nullopt;
    }
    const double percent = 100.0 * memory->used / memory->total;
    value->setText(QString::number(percent, 'f', 0) + "%");
    detail->setText(QString("%1 / %2 GiB used").arg(memory->used / GiB, 0, 'f', 2)
                   .arg(memory->total / GiB, 0, 'f', 2));
    return percent;
}
}

Poller::Poller() : timer(new QTimer(this)) {
    timer->setInterval(1000);
    timer->setTimerType(Qt::CoarseTimer);
    connect(timer, &QTimer::timeout, this, &Poller::poll);
}

void Poller::start() {
    systemReader.resetCpu();
    nextDiskRead = nextGpuRetry = 0;
    processTicksRemaining = 0;
    timer->start();
    poll();
}

void Poller::poll() {
    const auto now = bootMilliseconds();
    if (gpu.gpuPercent || gpu.memoryUsed || now >= nextGpuRetry) {
        gpu = reader.read();
        nextGpuRetry = now + 5000;
    }
    const bool refreshDisk = now >= nextDiskRead;
    const auto system = systemReader.read(refreshDisk);
    if (refreshDisk) nextDiskRead = now + 15000;
    // CPU and RAM keep updating even when the NVIDIA driver is unavailable.
    MonitorSample sample{gpu, system, std::nullopt};
    if (processesEnabled && (processTicksRemaining == 0 || --processTicksRemaining == 0)) {
        sample.processes = reader.readProcesses();
        processTicksRemaining = 3;
    }
    emit sampled(sample);
}

void Poller::setProcessesEnabled(bool enabled) {
    processesEnabled = enabled;
    processTicksRemaining = 0;
}

void Poller::setPaused(bool paused) {
    if (paused) timer->stop();
    else start();
}

ProcessPanel::ProcessPanel(bool memoryView, const QString& name, QWidget* parent)
    : QWidget(parent), memory(memoryView) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 4, 0, 0);
    toggle = new QToolButton(this);
    toggle->setObjectName(name + "-toggle");
    toggle->setText("Processes");
    toggle->setCheckable(true);
    toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toggle->setArrowType(Qt::RightArrow);
    layout->addWidget(toggle);
    body = new QWidget(this);
    auto* content = new QVBoxLayout(body);
    content->setContentsMargins(0, 0, 0, 0);
    table = new QTableWidget(0, 3, body);
    table->setObjectName(name + "-table");
    table->setHorizontalHeaderLabels({"Process", "PID", memory ? "MiB" : "Type"});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setAlternatingRowColors(true);
    table->verticalHeader()->hide();
    auto font = table->font();
    font.setPointSize(9);
    table->setFont(font);
    table->verticalHeader()->setDefaultSectionSize(QFontMetrics(font).height() + 8);
    table->horizontalHeader()->setFont(font);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table->setFixedHeight(180);
    table->setToolTip(memory ? "Driver-reported allocations can differ from total VRAM due to shared memory and driver overhead." :
        "G = graphics; C = compute. A process holding a GPU context may be idle; these are not per-process load percentages.");
    content->addWidget(table);
    message = label("Waiting for process data…", 8);
    message->setTextFormat(Qt::PlainText);
    message->setWordWrap(true);
    content->addWidget(message);
    layout->addWidget(body);
    body->hide();
    connect(toggle, &QToolButton::toggled, this, [this](bool expanded) {
        toggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        body->setVisible(expanded);
        if (expanded) {
            table->setRowCount(0);
            message->setText("Waiting for process data…");
        }
    });
}

void ProcessPanel::showProcesses(const GpuProcesses& processes) {
    if (!toggle->isChecked()) return;
    auto rows = processes.rows;
    if (memory) std::sort(rows.begin(), rows.end(), [](const GpuProcess& a, const GpuProcess& b) {
        if (a.memoryUsed != b.memoryUsed) return a.memoryUsed > b.memoryUsed;
        return a.pid < b.pid;
    });
    const int scroll = table->verticalScrollBar()->value();
    table->setUpdatesEnabled(false);
    table->setRowCount(int(rows.size()));
    for (int row = 0; row < int(rows.size()); ++row) {
        const auto& process = rows[row];
        auto* name = new QTableWidgetItem(QString::fromStdString(process.name));
        name->setToolTip(name->text());
        table->setItem(row, 0, name);
        table->setItem(row, 1, new QTableWidgetItem(QString::number(process.pid)));
        const QString value = memory ? (process.memoryUsed ? QString::number(*process.memoryUsed / 1048576.0, 'f', 1) : "—") :
            (process.graphics && process.compute ? "G + C" : process.graphics ? "G" : "C");
        table->setItem(row, 2, new QTableWidgetItem(value));
    }
    table->verticalScrollBar()->setValue(scroll);
    table->setUpdatesEnabled(true);
    QString summary = rows.empty() ? "No GPU processes reported." :
        memory ? "Largest allocations first · refreshes every 3 seconds" : "G: graphics · C: compute · contexts may be idle";
    if (!processes.error.empty()) summary = QString::fromStdString(processes.error) +
        (rows.empty() ? "" : " · Showing available data");
    message->setText(summary);
}

Chart::Chart(QColor color, QWidget* parent) : QWidget(parent), accent(color) {
    setMinimumHeight(64);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAccessibleName("Utilization history, 0 to 100 percent over two minutes");
}

void Chart::append(std::optional<double> percent, qint64 milliseconds) {
    history.push_back({milliseconds, percent});
    while (history.size() > 120 || (!history.empty() && history.front().time < milliseconds - HistoryMs))
        history.pop_front();
    update();
}

void Chart::clear() {
    history.clear();
    update();
}

void Chart::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    auto font = painter.font();
    font.setPointSize(8);
    painter.setFont(font);
    const QFontMetricsF metrics(font);
    const double verticalMargin = metrics.height() / 2 + 2;
    const double axisWidth = metrics.horizontalAdvance("100") + 6;
    const QRectF plot(2, verticalMargin, std::max(0.0, width() - axisWidth - 4),
                      std::max(0.0, height() - 2 * verticalMargin));
    auto grid = palette().color(QPalette::Text);
    grid.setAlpha(28);
    painter.setPen(QPen(grid, 1));
    for (double fraction : {0.0, 0.5, 1.0}) {
        const double y = plot.top() + plot.height() * fraction;
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    }
    painter.setPen(palette().color(QPalette::Disabled, QPalette::Text));
    const double baseline = (metrics.ascent() - metrics.descent()) / 2;
    painter.drawText(QPointF(width() - metrics.horizontalAdvance("100") - 2, plot.top() + baseline), "100");
    painter.drawText(QPointF(width() - metrics.horizontalAdvance("0") - 2, plot.bottom() + baseline), "0");
    if (history.empty()) return;

    const qint64 latest = history.back().time;
    QPainterPath line;
    bool connected = false;
    qint64 previous = 0;
    QPointF last;
    for (const auto& sample : history) {
        if (!sample.value) { connected = false; continue; }
        const double x = plot.right() - double(latest - sample.time) / HistoryMs * plot.width();
        const double y = plot.bottom() - std::clamp(*sample.value, 0.0, 100.0) / 100.0 * plot.height();
        // Driver delays, suspend, and missing readings create a visible gap.
        if (!connected || sample.time - previous > 2500) line.moveTo(x, y);
        else line.lineTo(x, y);
        connected = true;
        previous = sample.time;
        last = QPointF(x, y);
    }
    painter.setClipRect(plot.adjusted(-2, -2, 2, 2));
    painter.setPen(QPen(accent, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(line);
    if (history.back().value) {
        painter.setBrush(accent);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(last, 3, 3);
    }
}

DiskPie::DiskPie(QWidget* parent) : QWidget(parent) {
    setFixedSize(112, 112);
    setObjectName("disk-pie");
    setAccessibleName("Root partition usage pie chart");
}

void DiskPie::setUsage(std::optional<DiskUsage> next) {
    if (usage && next && usage->total == next->total && usage->used == next->used &&
        usage->available == next->available && usage->reserved == next->reserved) return;
    usage = next;
    if (usage) setAccessibleName(QString("Root partition: %1 percent used, %2 GiB available, %3 GiB reserved")
        .arg(100.0 * usage->used / usage->total, 0, 'f', 1)
        .arg(usage->available / GiB, 0, 'f', 1).arg(usage->reserved / GiB, 0, 'f', 1));
    else setAccessibleName("Root partition usage unavailable");
    update();
}

void DiskPie::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    const QRectF circle(4, 4, width() - 8, height() - 8);
    if (!usage || !usage->total) {
        painter.setBrush(palette().color(QPalette::Mid));
        painter.drawEllipse(circle);
        return;
    }
    const int used = qRound(5760.0 * usage->used / usage->total);
    const int available = qRound(5760.0 * usage->available / usage->total);
    painter.setBrush(ReservedColor);
    painter.drawEllipse(circle);
    painter.setBrush(RamColor);
    painter.drawPie(circle, 1440, -used);
    painter.setBrush(VramColor);
    painter.drawPie(circle, 1440 - used, -available);
}

MonitorWindow::MonitorWindow() {
    setWindowTitle("System Monitor");
    setMinimumSize(620, 480);
    resize(720, std::min(940, QGuiApplication::primaryScreen()->availableGeometry().height() - 80));
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget;
    scroll->setWidget(content);
    outer->addWidget(scroll);
    auto* layout = new QVBoxLayout(content);
    layout->setSizeConstraint(QLayout::SetMinimumSize);
    layout->setContentsMargins(18, 16, 18, 14);
    layout->setSpacing(10);
    layout->addWidget(label("System Monitor", 17, true));
    device = label("Connecting to NVIDIA GPU…", 10);
    device->setWordWrap(true);
    layout->addWidget(device);
    auto* metrics = new QGridLayout;
    metrics->setSpacing(10);
    metrics->setColumnStretch(0, 1);
    metrics->setColumnStretch(1, 1);
    QLabel* cpuDetail;
    metrics->addWidget(metricCard("CPU LOAD", CpuColor, cpuValue, cpuDetail, cpuChart, this), 0, 0);
    cpuDetail->setText("All CPU cores");
    cpuValue->setObjectName("cpu-value");
    cpuValue->setAccessibleName("Total CPU load percentage");
    metrics->addWidget(metricCard("SYSTEM RAM", RamColor, ramValue, ramDetail, ramChart, this), 0, 1);
    ramValue->setObjectName("ram-value");
    ramValue->setAccessibleName("System RAM used percentage");
    ramDetail->setObjectName("ram-detail");
    ramDetail->setToolTip("Used RAM = total minus available. Reclaimable cache counts as available.");
    QLabel* gpuDetail;
    auto* gpuCard = metricCard("GPU LOAD", GpuColor, gpuValue, gpuDetail, gpuChart, this);
    metrics->addWidget(gpuCard, 1, 0, Qt::AlignTop);
    gpuProcesses = new ProcessPanel(false, "gpu-processes", gpuCard);
    gpuCard->layout()->addWidget(gpuProcesses);
    gpuDetail->setText("GPU busy time");
    gpuValue->setObjectName("gpu-value");
    gpuValue->setAccessibleName("GPU load percentage");
    auto* vramCard = metricCard("VRAM", VramColor, vramValue, vramDetail, vramChart, this);
    metrics->addWidget(vramCard, 1, 1, Qt::AlignTop);
    vramProcesses = new ProcessPanel(true, "vram-processes", vramCard);
    vramCard->layout()->addWidget(vramProcesses);
    vramValue->setObjectName("vram-value");
    vramValue->setAccessibleName("VRAM used percentage");
    vramDetail->setObjectName("vram-detail");
    layout->addLayout(metrics, 1);
    layout->addWidget(label("History · last 2 minutes", 9));

    auto* diskCard = new QFrame(this);
    diskCard->setObjectName("metric");
    diskCard->setStyleSheet(CardStyle);
    auto* diskLayout = new QHBoxLayout(diskCard);
    diskLayout->setContentsMargins(16, 10, 16, 10);
    diskLayout->setSpacing(16);
    diskPie = new DiskPie(diskCard);
    diskLayout->addWidget(diskPie);
    auto* diskText = new QVBoxLayout;
    auto* diskHeading = new QHBoxLayout;
    diskHeading->addWidget(label("ROOT PARTITION · /", 10, true));
    diskHeading->addStretch();
    diskValue = label("—", 22, true);
    diskValue->setObjectName("disk-value");
    diskHeading->addWidget(diskValue);
    diskText->addLayout(diskHeading);
    diskDetail = label("Waiting for reading…", 10);
    diskDetail->setObjectName("disk-detail");
    diskText->addWidget(diskDetail);
    diskLegend = label("", 9);
    diskLegend->setWordWrap(true);
    diskLegend->setToolTip("Reserved space is kept by the filesystem for privileged use. The pie uses the full partition capacity.");
    diskText->addWidget(diskLegend);
    diskLayout->addLayout(diskText, 1);
    layout->addWidget(diskCard);

    error = label("", 9);
    error->setWordWrap(true);
    error->setTextFormat(Qt::PlainText);
    error->hide();
    layout->addWidget(error);
    auto* controls = new QHBoxLayout;
    pin = new QCheckBox("Always on top");
    pin->setObjectName("pin");
    controls->addWidget(pin);
    controls->addStretch();
    pause = new QPushButton("Pause");
    pause->setObjectName("pause");
    pause->setCheckable(true);
    controls->addWidget(pause);
    layout->addLayout(controls);
    status = label("Connecting…", 9);
    status->setObjectName("status");
    layout->addWidget(status);

    connect(pin, &QCheckBox::toggled, this, [this](bool checked) {
        const auto position = pos();
        setWindowFlag(Qt::WindowStaysOnTopHint, checked);
        show();
        move(position);
    });
    qRegisterMetaType<MonitorSample>();
    poller = new Poller;
    poller->moveToThread(&worker);
    auto processToggled = [this] {
        const bool enabled = gpuProcesses->toggle->isChecked() || vramProcesses->toggle->isChecked();
        QMetaObject::invokeMethod(poller, [this, enabled] { poller->setProcessesEnabled(enabled); }, Qt::QueuedConnection);
    };
    connect(gpuProcesses->toggle, &QToolButton::toggled, this, processToggled);
    connect(vramProcesses->toggle, &QToolButton::toggled, this, processToggled);
    connect(&worker, &QThread::started, poller, &Poller::start);
    connect(&worker, &QThread::finished, poller, &QObject::deleteLater);
    connect(poller, &Poller::sampled, this, &MonitorWindow::showSample);
    connect(pause, &QPushButton::toggled, poller, &Poller::setPaused);
    connect(pause, &QPushButton::toggled, this, [this](bool checked) {
        paused = checked;
        pause->setText(checked ? "Resume" : "Pause");
        status->setText(checked ? "Paused · readings frozen" : "Resuming…");
        if (!checked) { cpuChart->clear(); ramChart->clear(); gpuChart->clear(); vramChart->clear(); }
    });
    worker.start(QThread::LowPriority);
}

MonitorWindow::~MonitorWindow() {
    worker.quit();
    worker.wait();
}

void MonitorWindow::showSample(const MonitorSample& sample) {
    if (paused) return;
    const auto& gpu = sample.gpu;
    const auto& system = sample.system;
    if (sample.processes) {
        gpuProcesses->showProcesses(*sample.processes);
        vramProcesses->showProcesses(*sample.processes);
    }
    if (!gpu.name.empty()) device->setText(QString::fromStdString(gpu.name));
    else device->setText("NVIDIA GPU");
    gpuValue->setText(gpu.gpuPercent ? QString::number(*gpu.gpuPercent) + "%" : "—");
    cpuValue->setText(system.cpuPercent ? QString::number(*system.cpuPercent, 'f', 0) + "%" : "—");
    std::optional<MemoryUsage> vram;
    if (gpu.memoryUsed && gpu.memoryTotal) vram = MemoryUsage{*gpu.memoryUsed, *gpu.memoryTotal};
    const auto vramPercent = showMemory(vram, vramValue, vramDetail);
    const auto ramPercent = showMemory(system.memory, ramValue, ramDetail);
    diskPie->setUsage(system.disk);
    if (system.disk) {
        const auto& disk = *system.disk;
        diskValue->setText(QString::number(100.0 * disk.used / disk.total, 'f', 0) + "%");
        diskDetail->setText(QString("%1 / %2 GiB used").arg(disk.used / GiB, 0, 'f', 1).arg(disk.total / GiB, 0, 'f', 1));
        diskLegend->setText(QString("<span style='color:%1'>● Used</span> &nbsp; "
            "<span style='color:%2'>● Available: %3 GiB</span><br>"
            "<span style='color:%4'>● Reserved: %5 GiB</span>")
            .arg(RamColor.name(), VramColor.name()).arg(disk.available / GiB, 0, 'f', 1)
            .arg(ReservedColor.name()).arg(disk.reserved / GiB, 0, 'f', 1));
    } else {
        diskValue->setText("—");
        diskDetail->setText("Unavailable");
        diskLegend->clear();
    }
    // Unlike CLOCK_MONOTONIC, CLOCK_BOOTTIME includes Linux suspend time, so
    // waking the computer cannot make old readings look like recent history.
    const qint64 milliseconds = bootMilliseconds();
    cpuChart->append(system.cpuPercent, milliseconds);
    ramChart->append(ramPercent, milliseconds);
    gpuChart->append(gpu.gpuPercent, milliseconds);
    vramChart->append(vramPercent, milliseconds);
    auto errors = gpu.error;
    if (!system.error.empty()) {
        if (!errors.empty()) errors += "; ";
        errors += system.error;
    }
    error->setText(QString::fromStdString(errors));
    error->setVisible(!errors.empty());
    status->setText(errors.empty() ? "Live · 1 second refresh · disk every 15 seconds" :
                    "Some readings unavailable · retrying automatically");
}
