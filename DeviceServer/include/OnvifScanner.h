#ifndef ONVIF_SCANNER_H
#define ONVIF_SCANNER_H

#include <vector>
#include <thread>
#include <atomic>
#include <functional>
#include <string>

#include "Common.h"

class OnvifScanner {
public:
    OnvifScanner();
    ~OnvifScanner();

    void start(std::atomic<bool>& is_running);
    void stop();

    // 콜백 설정 (DeviceManager가 호출)
    using DeviceFoundCallback = std::function<void(const DeviceInfo&)>;
    void setOnDeviceFound(DeviceFoundCallback cb) { on_device_found_ = cb; }

private:
    void runScan();
    std::vector<std::string> getRtspUrls(const std::string& ip);
    std::string getSingleRtspUrl(const std::string& ip);

    std::atomic<bool>* is_running_ = nullptr;
    std::thread scan_thread_;

    DeviceFoundCallback on_device_found_;
};

#endif
