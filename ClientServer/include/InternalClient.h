#ifndef INTERNAL_CLIENT_H
#define INTERNAL_CLIENT_H

#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <condition_variable>

#include "Common.h"
#include "json.hpp"

using json = nlohmann::json;

class InternalClient {
public:
    InternalClient();
    ~InternalClient();

    // DeviceServer에 접속 (백그라운드 스레드에서 주기적으로 카메라 리스트 수신)
    void start(const std::string& auth_host, int auth_port);
    void stop();

    // 현재 캐시된 카메라 리스트 가져오기
    json getCameraList();

    // DeviceServer로 모터 제어 명령 전달
    void sendDeviceCommand(const json& body);

    // 현재 캐시된 디바이스 상태 가져오기
    json getDeviceStatus();

    // AI 이벤트 수신 콜백 설정
    using AiEventCallback = std::function<void(const json& event)>;
    void setOnAiEvent(AiEventCallback cb) { on_ai_event_ = cb; }

    // IMAGE 이벤트 수신 콜백 설정
    using ImageEventCallback = std::function<void(const json& meta, const std::vector<char>& jpeg)>;
    void setOnImageEvent(ImageEventCallback cb) { on_image_event_ = cb; }

    // META 센서 데이터 수신 콜백 설정
    using MetaEventCallback = std::function<void(const json& sensor_data)>;
    void setOnMetaEvent(MetaEventCallback cb) { on_meta_event_ = cb; }

private:
    // DeviceServer에 접속 시도 (재연결 포함)
    void connectionLoop();

    // DeviceServer에 CAMERA 요청 보내고 응답 받기
    json requestCameraList(int sock_fd);

    // DeviceServer에서 오는 패킷 수신 (카메라 응답 + AI 이벤트)
    bool handleIncoming(int sock_fd);

    // DeviceServer에 AVAILABLE 요청 보내고 응답 받기
    json requestDeviceStatus(int sock_fd);

    // expected_type 응답이 올 때까지 대기 (Push 이벤트는 디스패치, 타임아웃 포함)
    json waitForResponse(int sock_fd, MessageType expected_type);

    // Push 이벤트(AI/IMAGE/META)를 콜백으로 디스패치
    void dispatchPushEvent(const PacketHeader& header, const std::vector<char>& body_buf, int sock_fd);

    bool sendMessage(int fd, MessageType type, const json& body);

    std::string auth_host_;
    int auth_port_ = 30000;
    std::thread conn_thread_;
    std::atomic<bool> is_running_{false};

    // 현재 연결 중인 소켓 fd (stop()에서 shutdown 용)
    int current_fd_ = -1;
    std::mutex fd_mutex_;

    // 캐시된 카메라 리스트 (스레드 안전)
    json cached_cameras_;
    json cached_device_status_;  // 디바이스 상태 캐시
    std::mutex cache_mutex_;

    // 종료 지연 시간(sleep_for) 방지용
    std::condition_variable cv_;
    std::mutex cv_mutex_;

    // AI 이벤트 콜백
    AiEventCallback on_ai_event_;

    // IMAGE 이벤트 콜백
    ImageEventCallback on_image_event_;

    // META 콜백
    MetaEventCallback on_meta_event_;
};

#endif
