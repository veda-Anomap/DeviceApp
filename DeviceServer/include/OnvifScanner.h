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

    // 중복 체크 콜백 (curl 호출 전에 확인)
    using IsRegisteredCallback = std::function<bool(const std::string& device_id)>;
    void setIsDeviceRegistered(IsRegisteredCallback cb) { is_registered_ = cb; }

private:
    void runScan();
    std::vector<std::string> getRtspUrls(const std::string& ip);
    std::string getSingleRtspUrl(const std::string& ip);

    // ARP 폴백 스캔 (IGMP Snooping 환경 대응)
    void arpFallbackScan();
    std::vector<std::string> getHanwhaIpsFromArp();

    std::atomic<bool>* is_running_ = nullptr;
    std::thread scan_thread_;

    DeviceFoundCallback on_device_found_;
    IsRegisteredCallback is_registered_;
};

#endif
