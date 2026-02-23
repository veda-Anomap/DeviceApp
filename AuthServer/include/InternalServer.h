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

class InternalServer {
public:
    InternalServer();
    ~InternalServer();

    // TCP 서버 시작 (StreamServer의 접속을 받음)
    void start(int port, CameraListProvider provider);
    void stop();

private:
    void acceptLoop();
    void clientHandler(int client_fd);
    bool recvExact(int fd, void* buf, size_t len);
    bool sendMessage(int client_fd, MessageType type, const json& body);

    int server_fd_ = -1;
    int port_ = 30000;
    std::thread accept_thread_;
    std::atomic<bool> is_running_{false};

    // 연결된 StreamServer fd
    std::vector<int> client_fds_;
    std::mutex client_mutex_;

    CameraListProvider get_camera_list_;
};

#endif
