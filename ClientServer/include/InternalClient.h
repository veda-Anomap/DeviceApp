#ifndef INTERNAL_CLIENT_H
#define INTERNAL_CLIENT_H

#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>

#include "Common.h"
#include "json.hpp"

using json = nlohmann::json;

class InternalClient {
public:
    InternalClient();
    ~InternalClient();

    // AuthServer에 접속 (백그라운드 스레드에서 주기적으로 카메라 리스트 수신)
    void start(const std::string& auth_host, int auth_port);
    void stop();

    // 현재 캐시된 카메라 리스트 가져오기
    json getCameraList();

private:
    // AuthServer에 접속 시도 (재연결 포함)
    void connectionLoop();

    // AuthServer에 CAMERA 요청 보내고 응답 받기
    json requestCameraList(int sock_fd);

    bool recvExact(int fd, void* buf, size_t len);
    bool sendMessage(int fd, MessageType type, const json& body);

    std::string auth_host_;
    int auth_port_ = 30000;
    std::thread conn_thread_;
    std::atomic<bool> is_running_{false};

    // 캐시된 카메라 리스트 (스레드 안전)
    json cached_cameras_;
    std::mutex cache_mutex_;
};

#endif
