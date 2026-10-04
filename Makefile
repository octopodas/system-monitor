CXX ?= g++
CPPFLAGS += -Isrc
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++17 -Wall -Wextra -Wpedantic -fPIC
QT_CFLAGS := $(shell pkg-config --cflags Qt5Widgets)
QT_LIBS := $(shell pkg-config --libs Qt5Widgets)
SOURCES := src/monitor.cpp src/telemetry.cpp src/system_metrics.cpp
HEADERS := src/monitor.h src/telemetry.h src/system_metrics.h
PREFIX ?= $(HOME)/.local

.PHONY: all test clean install uninstall
all: build/system-monitor

build build/tests:
	mkdir -p $@

build/moc_monitor.cpp: $(HEADERS) | build
	moc $(QT_CFLAGS) $< -o $@

build/system-monitor: src/main.cpp $(SOURCES) $(HEADERS) build/moc_monitor.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(QT_CFLAGS) src/main.cpp $(SOURCES) build/moc_monitor.cpp -o $@ $(QT_LIBS) -ldl

build/tests/libnvidia-ml.so.1: tests/fake_nvml.cpp | build/tests
	$(CXX) $(CXXFLAGS) -shared $< -o $@

build/tests/test-telemetry: tests/test_telemetry.cpp src/telemetry.cpp src/telemetry.h | build/tests
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/test_telemetry.cpp src/telemetry.cpp -o $@ -ldl

build/tests/test-system: tests/test_system_metrics.cpp src/system_metrics.cpp src/system_metrics.h | build/tests
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/test_system_metrics.cpp src/system_metrics.cpp -o $@

build/tests/test-ui: tests/test_ui.cpp $(SOURCES) $(HEADERS) build/moc_monitor.cpp | build/tests
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(QT_CFLAGS) $(shell pkg-config --cflags Qt5Test) tests/test_ui.cpp $(SOURCES) build/moc_monitor.cpp -o $@ $(QT_LIBS) $(shell pkg-config --libs Qt5Test) -ldl

test: build/tests/libnvidia-ml.so.1 build/tests/test-telemetry build/tests/test-system build/tests/test-ui
	./build/tests/test-telemetry
	./build/tests/test-system
	QT_QPA_PLATFORM=offscreen LD_LIBRARY_PATH="$(CURDIR)/build/tests" ./build/tests/test-ui

install: build/system-monitor
	install -Dsm755 build/system-monitor "$(DESTDIR)$(PREFIX)/bin/system-monitor"
	install -Dm644 assets/system-monitor.svg "$(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/local-system-monitor.svg"
	mkdir -p "$(DESTDIR)$(PREFIX)/share/applications"
	python3 tools/install_desktop.py "$(PREFIX)" "$(DESTDIR)$(PREFIX)/share/applications/local-system-monitor.desktop"

uninstall:
	rm -f "$(DESTDIR)$(PREFIX)/bin/system-monitor" "$(DESTDIR)$(PREFIX)/share/applications/local-system-monitor.desktop" "$(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/local-system-monitor.svg"

clean:
	rm -rf build
