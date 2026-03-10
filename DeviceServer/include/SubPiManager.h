#ifndef SUB_PI_MANAGER_H
#define SUB_PI_MANAGER_H

#include <vector>
#include <thread>
#include <atomic>
#include <functional>
#include <string>

#include "Common.h"
#include "json.hpp"

using json = nlohmann::json;

class SubPiManager {
public:
    SubPiManager();
    ~SubPiManager();

    // 비콘 수신 시작/중지
    void start(std::atomic<bool>& is_running);
    void stop();

    // 콜백 설정 (DeviceManager가 호출)
    using DeviceFoundCallback = std::function<void(const DeviceInfo&, int tcp_fd)>;
    using AiEventCallback = std::function<void(const std::string& device_id, const json& event)>;
    using ImageEventCallback = std::function<void(const std::string& device_id, const json& meta, const std::vector<char>& jpeg)>;
    using AvailableEventCallback = std::function<void(const std::string& device_id, const json& status)>;
    using MetaEventCallback = std::function<void(const std::string& device_id, const json& sensor_data)>;
    void setOnDeviceFound(DeviceFoundCallback cb) { on_device_found_ = cb; }
    void setOnAiEvent(AiEventCallback cb) { on_ai_event_ = cb; }
    void setOnImageEvent(ImageEventCallback cb) { on_image_event_ = cb; }
    void setOnAvailableEvent(AvailableEventCallback cb) { on_available_event_ = cb; }
    void setOnMetaEvent(MetaEventCallback cb) { on_meta_event_ = cb; }

    // 중복 체크 콜백 (TCP 연결 전에 확인)
    using IsRegisteredCallback = std::function<bool(const std::string& device_id)>;
    void setIsDeviceRegistered(IsRegisteredCallback cb) { is_registered_ = cb; }

private:
    void runBeaconReceiver();
    bool requestStartStream(const std::string& target_ip, int listen_port, int& tcp_socket);
    void subPiListener(std::string device_id, int socket_fd);
    bool recvExact(int fd, void* buf, size_t len);

    std::atomic<bool>* is_running_ = nullptr;
    std::thread beacon_thread_;
    std::vector<std::thread> listener_threads_;
    int next_port_ = 15001;

    DeviceFoundCallback on_device_found_;
    AiEventCallback on_ai_event_;
    ImageEventCallback on_image_event_;
    AvailableEventCallback on_available_event_;
    MetaEventCallback on_meta_event_;
    IsRegisteredCallback is_registered_;
};

#endif
