#include <fstream>
#include <sstream>
#include <sys/sysinfo.h>

#include "SystemMonitor.h"

SystemMonitor::SystemMonitor() {
    // 첫 호출 시 delta 계산을 위해 초기값 세팅
    getCpuUsagePercent();
}

json SystemMonitor::getStatus() {
    json status;
    status["cpu"]    = getCpuUsagePercent();    // %
    status["memory"] = getMemoryUsagePercent();  // %
    status["temp"]   = getTemperature();          // °C
    status["uptime"] = getUptimeSeconds();        // 초
    return status;
}

// ======================== 온도 ========================
// /sys/class/thermal/thermal_zone0/temp → 45000 → 45.0°C
double SystemMonitor::getTemperature() {
    std::ifstream file("/sys/class/thermal/thermal_zone0/temp");
    double temp = 0.0;
    if (file.is_open()) {
        file >> temp;
    }
    return temp / 1000.0;
}

// ======================== 메모리 ========================
double SystemMonitor::getMemoryUsagePercent() {
    struct sysinfo info;
    if (sysinfo(&info) != 0) return 0.0;

    long total = info.totalram * info.mem_unit;
    long free_mem = info.freeram * info.mem_unit;
    long used = total - free_mem;

    if (total == 0) return 0.0;
    return (double)used / total * 100.0;
}

// ======================== CPU 사용량 ========================
// /proc/stat의 첫 줄에서 이전 측정값과의 차이로 사용률 계산
double SystemMonitor::getCpuUsagePercent() {
    std::ifstream file("/proc/stat");
    if (!file.is_open()) return 0.0;

    std::string cpu_label;
    long user, nice, system, idle, iowait, irq, softirq, steal;

    file >> cpu_label >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;

    long cur_idle = idle + iowait;
    long cur_total = user + nice + system + idle + iowait + irq + softirq + steal;

    long diff_idle = cur_idle - prev_idle_;
    long diff_total = cur_total - prev_total_;

    prev_idle_ = cur_idle;
    prev_total_ = cur_total;

    if (diff_total == 0) return 0.0;
    return (1.0 - (double)diff_idle / diff_total) * 100.0;
}

// ======================== 서버 가동 시간 ========================
long SystemMonitor::getUptimeSeconds() {
    struct sysinfo info;
    if (sysinfo(&info) != 0) return 0;
    return info.uptime;
}
