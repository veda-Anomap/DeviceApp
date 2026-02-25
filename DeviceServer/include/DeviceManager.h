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

using json = nlohmann::json;


class DeviceManager {
public:
    DeviceManager();
    ~DeviceManager();

    // 장치 탐색 시작 (ONVIF 및 UDP Beacon 스레드 실행)
    void startDiscovery();
    void stopDiscovery();

    std::vector<DeviceInfo> getDeviceList();

    // 새 장치 등록 시 호출되는 콜백 설정
    using DeviceCallback = std::function<void(const DeviceInfo&)>;
    void setOnDeviceRegistered(DeviceCallback cb) { on_device_registered_ = cb; }

    // 장치 연결 끊김 시 호출되는 콜백
    using DeviceRemovedCallback = std::function<void(const std::string& device_id)>;
    void setOnDeviceRemoved(DeviceRemovedCallback cb) { on_device_removed_ = cb; }

    // AI 이벤트 수신 콜백
    using AiEventCallback = std::function<void(const std::string& device_id, const json& event)>;
    void setOnAiEvent(AiEventCallback cb) { on_ai_event_ = cb; }
private:
    // 1. 스레드 관리를 위한 벡터
    std::vector<std::thread> discovery_threads_;

    // 2. 스레드 루프 제어를 위한 원자적(Atomic) 플래그
    // 여러 스레드가 동시에 참조해도 안전하도록 std::atomic을 사용합니다.
    std::atomic<bool> is_discovering_;
    
    // 3. 장치 리스트 및 동기화 뮤텍스
    std::map<std::string, DeviceInfo> devices_;
    std::mutex device_mutex_;

    // 4. 장치 생존 여부를 체크하는 플래그
    bool is_monitoring_ = false;

     // UDP 포트 시작 번호
    int next_port = 15001;

    // 새 장치 등록 콜백
    DeviceCallback on_device_registered_;

    // 장치 제거 콜백
    DeviceRemovedCallback on_device_removed_;

    // AI 이벤트 콜백
    AiEventCallback on_ai_event_;

    // Sub-Pi 리스너 스레드들
    std::vector<std::thread> listener_threads_;

    // 스레드에서 실행될 함수들
    void runBeaconReceiver();   // UDP Beacon 감지
    void runOnvifScanner();     // ONVIF 스캔
    void monitorLoop();         // 생존 여부
    void subPiListener(std::string device_id, int socket_fd); // AI 이벤트 수신
    bool requestStartStream(const std::string& target_ip, int listen_port, int& tcp_socket); 
    std::vector<std::string> getRtspUrls(const std::string& ip);
    std::string getSingleRtspUrl(const std::string& ip);
    bool recvExact(int fd, void* buf, size_t len);
};

#endif