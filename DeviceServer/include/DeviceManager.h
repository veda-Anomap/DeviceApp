#ifndef DEVICE_MANAGER_H
#define DEVICE_MANAGER_H

#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <string>
#include <map>
#include <functional>

#include "Common.h"
#include "json.hpp"
#include "SubPiManager.h"
#include "OnvifScanner.h"

using json = nlohmann::json;

class DeviceManager {
public:
    DeviceManager();
    ~DeviceManager();

    // 장치 탐색 시작/중지
    void startDiscovery();
    void stopDiscovery();

    std::vector<DeviceInfo> getDeviceList();
    json getDeviceStatusList();

    // 외부 콜백 (DeviceController가 설정)
    using DeviceCallback = std::function<void(const DeviceInfo&)>;
    void setOnDeviceRegistered(DeviceCallback cb) { on_device_registered_ = cb; }

    using DeviceRemovedCallback = std::function<void(const std::string& device_id)>;
    void setOnDeviceRemoved(DeviceRemovedCallback cb) { on_device_removed_ = cb; }

    using AiEventCallback = std::function<void(const std::string& device_id, const json& event)>;
    void setOnAiEvent(AiEventCallback cb) { on_ai_event_ = cb; }

    using ImageEventCallback = std::function<void(const std::string& device_id, const json& meta, const std::vector<char>& jpeg)>;
    void setOnImageEvent(ImageEventCallback cb) { on_image_event_ = cb; }

private:
    void monitorLoop();

    // 하위 매니저 (콜백으로만 통신)
    SubPiManager subpi_mgr_;
    OnvifScanner onvif_scanner_;

    // 장치 데이터 (유일한 소유자!)
    std::map<std::string, DeviceInfo> devices_;
    std::map<std::string, json> device_status_;  // SubCam 시스템 상태 캐시
    std::mutex device_mutex_;

    // 스레드 제어
    std::atomic<bool> is_discovering_;
    std::thread monitor_thread_;

    // 외부 콜백
    DeviceCallback on_device_registered_;
    DeviceRemovedCallback on_device_removed_;
    AiEventCallback on_ai_event_;
    ImageEventCallback on_image_event_;
};

#endif