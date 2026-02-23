#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>

#include "InternalClient.h"

InternalClient::InternalClient() {
    cached_cameras_ = json::array();
}

InternalClient::~InternalClient() {
    stop();
}

void InternalClient::start(const std::string& auth_host, int auth_port) {
    if (is_running_) return;

    auth_host_ = auth_host;
    auth_port_ = auth_port;
    is_running_ = true;

    conn_thread_ = std::thread(&InternalClient::connectionLoop, this);

    std::cout << "[InternalClient] Started. Connecting to AuthServer at "
              << auth_host_ << ":" << auth_port_ << std::endl;
}

void InternalClient::stop() {
    if (!is_running_) return;
    is_running_ = false;

    if (conn_thread_.joinable()) {
        conn_thread_.join();
    }

    std::cout << "[InternalClient] Stopped." << std::endl;
}

json InternalClient::getCameraList() {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    return cached_cameras_;
}

void InternalClient::connectionLoop() {
    while (is_running_) {
        // 1. AuthServer에 TCP 접속
        int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (sock_fd < 0) {
            std::cerr << "[InternalClient] Socket creation failed." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(3));
            continue;
        }

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(auth_port_);
        inet_pton(AF_INET, auth_host_.c_str(), &addr.sin_addr);

        if (connect(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(sock_fd);
            std::cerr << "[InternalClient] Connection failed. Retrying in 3s..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(3));
            continue;
        }

        std::cout << "[InternalClient] Connected to AuthServer." << std::endl;

        // 2. 연결 유지하면서 주기적으로 카메라 리스트 요청
        while (is_running_) {
            json result = requestCameraList(sock_fd);

            if (result.is_null()) {
                std::cout << "[InternalClient] AuthServer disconnected. Reconnecting..." << std::endl;
                break; // 재연결 시도
            }

            // 캐시 업데이트
            {
                std::lock_guard<std::mutex> lock(cache_mutex_);
                cached_cameras_ = result;
            }

            // 5초 대기 후 다시 요청
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }

        close(sock_fd);
    }
}

json InternalClient::requestCameraList(int sock_fd) {
    // CAMERA 요청 전송 (빈 body)
    if (!sendMessage(sock_fd, MessageType::CAMERA, {})) {
        return nullptr;
    }

    // 응답 수신
    PacketHeader header;
    if (!recvExact(sock_fd, &header, sizeof(header))) {
        return nullptr;
    }

    uint32_t body_len = ntohl(header.body_length);
    if (body_len == 0 || body_len > 1024 * 1024) {
        return nullptr;
    }

    std::vector<char> buf(body_len);
    if (!recvExact(sock_fd, buf.data(), body_len)) {
        return nullptr;
    }

    try {
        return json::parse(std::string(buf.begin(), buf.end()));
    } catch (...) {
        return nullptr;
    }
}

bool InternalClient::sendMessage(int fd, MessageType type, const json& body) {
    std::string body_str = body.dump();
    PacketHeader header;
    header.type = type;
    header.body_length = htonl(static_cast<uint32_t>(body_str.size()));

    if (send(fd, &header, sizeof(header), MSG_NOSIGNAL) < 0) return false;
    if (!body_str.empty()) {
        if (send(fd, body_str.c_str(), body_str.size(), MSG_NOSIGNAL) < 0) return false;
    }
    return true;
}

bool InternalClient::recvExact(int fd, void* buf, size_t len) {
    size_t received = 0;
    char* ptr = static_cast<char*>(buf);
    while (received < len) {
        ssize_t n = recv(fd, ptr + received, len - received, 0);
        if (n <= 0) return false;
        received += n;
    }
    return true;
}
