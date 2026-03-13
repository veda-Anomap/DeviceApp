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

    // Sub-Pi에 모터 제어 명령 전송 (IP로 장치 조회 → TCP 전송)
    bool sendMotorCommand(const std::string& ip, const std::string& motor);

    // 외부 콜백 (DeviceController가 설정)
    using DeviceCallback = std::function<void(const DeviceInfo&)>;
    void setOnDeviceRegistered(DeviceCallback cb) { on_device_registered_ = cb; }

    using DeviceRemovedCallback = std::function<void(const std::string& device_id)>;
    void setOnDeviceRemoved(DeviceRemovedCallback cb) { on_device_removed_ = cb; }

    using AiEventCallback = std::function<void(const std::string& device_id, const json& event)>;
    void setOnAiEvent(AiEventCallback cb) { on_ai_event_ = cb; }

    using ImageEventCallback = std::function<void(const std::string& device_id, const json& meta, const std::vector<char>& jpeg)>;
    void setOnImageEvent(ImageEventCallback cb) { on_image_event_ = cb; }

    using MetaEventCallback = std::function<void(const std::string& device_id, const json& sensor_data)>;
    void setOnMetaEvent(MetaEventCallback cb) { on_meta_event_ = cb; }

    // 장치 변경(등록/제거/복구) 시 호출 — 락 해제 후 호출됨 (데드락 방지)
    using DeviceChangedCallback = std::function<void()>;
    void setOnDeviceChanged(DeviceChangedCallback cb) { on_device_changed_ = cb; }

private:
    void monitorLoop();
    bool checkTcpPort(const std::string& ip, int port, int timeout_sec = 2);

    // 하위 매니저 (콜백으로만 통신)
    SubPiManager subpi_mgr_;
    OnvifScanner onvif_scanner_;

    // 장치 데이터 (유일한 소유자!)
    std::map<std::string, DeviceInfo> devices_;
    std::map<std::string, json> device_status_;  // SubCam 시스템 상태 캐시
    std::mutex device_mutex_;

    // Sub-Pi command_socket_fd에 대한 send 직렬화
    std::mutex send_mutex_;

    // 스레드 제어
    std::atomic<bool> is_discovering_;
    std::thread monitor_thread_;

    // 외부 콜백
    DeviceCallback on_device_registered_;
    DeviceRemovedCallback on_device_removed_;
    DeviceChangedCallback on_device_changed_;
    AiEventCallback on_ai_event_;
    ImageEventCallback on_image_event_;
    MetaEventCallback on_meta_event_;
};

#endif