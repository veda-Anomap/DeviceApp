#ifndef SYSTEM_MONITOR_H
#define SYSTEM_MONITOR_H

#include "json.hpp"

using json = nlohmann::json;

class SystemMonitor {
public:
    SystemMonitor();

    // 전체 시스템 상태를 JSON으로 리턴
    json getStatus();

private:
    double getTemperature();
    double getMemoryUsagePercent();
    double getCpuUsagePercent();
    long getUptimeSeconds();

    // CPU 사용량 계산용 (이전 측정값)
    long prev_idle_ = 0;
    long prev_total_ = 0;
};

#endif
