#ifndef INTERNAL_SERVER_H
#define INTERNAL_SERVER_H

#include <thread>
#include <atomic>
#include <functional>
#include <vector>
#include <mutex>

#include "Common.h"
#include "json.hpp"

using json = nlohmann::json;

// 카메라 리스트를 가져오는 콜백 타입
using CameraListProvider = std::function<json()>;
// 디바이스 상태를 가져오는 콜백 타입
using DeviceStatusProvider = std::function<json()>;

class InternalServer {
public:
    InternalServer();
    ~InternalServer();

    // TCP 서버 시작 (StreamServer의 접속을 받음)
    void start(int port, CameraListProvider camera_provider, DeviceStatusProvider status_provider);
    void stop();

    // 연결된 모든 ClientServer에 AI 이벤트 전송
    void broadcastAiEvent(const json& event);

    // 연결된 모든 ClientServer에 IMAGE 이벤트 전송 (JSON 메타 + JPEG 바이너리)
    void broadcastImageEvent(const json& meta, const std::vector<char>& jpeg);

private:
    void acceptLoop();
    void clientHandler(int client_fd);
    bool recvExact(int fd, void* buf, size_t len);
    bool sendMessage(int client_fd, MessageType type, const json& body);
    bool sendImageMessage(int client_fd, const json& meta, const std::vector<char>& jpeg);

    int server_fd_ = -1;
    int port_ = 30000;
    std::thread accept_thread_;
    std::atomic<bool> is_running_{false};

    // 연결된 StreamServer fd
    std::vector<int> client_fds_;
    std::mutex client_mutex_;

    CameraListProvider get_camera_list_;
    DeviceStatusProvider get_device_status_;
};

#endif
