#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <sys/select.h>

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

    // 대기 중인 조건 변수 깨우기
    cv_.notify_all();

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
        // 1. DeviceServer에 TCP 접속
        int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (sock_fd < 0) {
            std::cerr << "[InternalClient] Socket creation failed." << std::endl;
            std::unique_lock<std::mutex> lock(cv_mutex_);
            cv_.wait_for(lock, std::chrono::seconds(3), [this] { return !is_running_; });
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
            std::unique_lock<std::mutex> lock(cv_mutex_);
            cv_.wait_for(lock, std::chrono::seconds(3), [this] { return !is_running_; });
            continue;
        }

        std::cout << "[InternalClient] Connected to DeviceServer." << std::endl;

        // 2. 연결 유지: 5초마다 카메라 요청 + 그 사이 AI 이벤트 수신
        auto last_request = std::chrono::steady_clock::now();

        while (is_running_) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_request).count();

            // 5초 경과 시 카메라 리스트 요청
            if (elapsed >= 5) {
                json result = requestCameraList(sock_fd);
                if (result.is_null()) {
                    std::cout << "[InternalClient] DeviceServer disconnected. Reconnecting..." << std::endl;
                    break;
                }
                {
                    std::lock_guard<std::mutex> lock(cache_mutex_);
                    cached_cameras_ = result;
                }
                last_request = std::chrono::steady_clock::now();
            }

            // select로 1초 대기하며 AI 이벤트 확인
            if (!handleIncoming(sock_fd)) {
                std::cout << "[InternalClient] DeviceServer disconnected (AI recv). Reconnecting..." << std::endl;
                break;
            }
        }

        close(sock_fd);
    }
}

bool InternalClient::handleIncoming(int sock_fd) {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(sock_fd, &read_fds);

    struct timeval tv;
    tv.tv_sec = 1;   // 1초 대기
    tv.tv_usec = 0;

    int ret = select(sock_fd + 1, &read_fds, nullptr, nullptr, &tv);
    if (ret < 0) return false;          // 에러
    if (ret == 0) return true;           // 타임아웃 (데이터 없음, 정상)

    // 데이터 있음 → 헤더 읽기
    PacketHeader header;
    if (!recvExact(sock_fd, &header, sizeof(header))) return false;

    uint32_t body_len = ntohl(header.body_length);
    if (body_len == 0 || body_len > 1024 * 1024) return true;

    std::vector<char> buf(body_len);
    if (!recvExact(sock_fd, buf.data(), body_len)) return false;

    try {
        json body = json::parse(std::string(buf.begin(), buf.end()));

        if (header.type == MessageType::AI) {
            std::cout << "[InternalClient] AI event received: " << body.dump() << std::endl;
            if (on_ai_event_) {
                on_ai_event_(body);
            }
        }
    } catch (...) {
        std::cerr << "[InternalClient] JSON parse error." << std::endl;
    }

    return true;
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
