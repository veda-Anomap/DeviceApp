#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <sys/select.h>
#include <chrono>

#include "InternalClient.h"
#include "NetUtil.h"

InternalClient::InternalClient() {
    cached_cameras_ = json::array();
    cached_device_status_ = json::array();
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

    // 현재 연결 중인 소켓 강제 종료 → recv()/select() 즉시 탈출
    {
        std::lock_guard<std::mutex> lock(fd_mutex_);
        if (current_fd_ != -1) {
            shutdown(current_fd_, SHUT_RDWR);
        }
    }

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

void InternalClient::sendDeviceCommand(const json& body) {
    std::lock_guard<std::mutex> lock(fd_mutex_);
    if (current_fd_ != -1) {
        sendMessage(current_fd_, MessageType::DEVICE, body);
    }
}

json InternalClient::getDeviceStatus() {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    return cached_device_status_;
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

        // 현재 소켓 fd 저장 (stop()에서 shutdown 가능하도록)
        {
            std::lock_guard<std::mutex> lock(fd_mutex_);
            current_fd_ = sock_fd;
        }

        // DeviceServer가 접속 즉시 CAMERA+AVAILABLE Push → 수신 대기
        // 이후 AI/IMAGE/META/CAMERA/AVAILABLE 모두 Push로 수신
        while (is_running_) {
            if (!handleIncoming(sock_fd)) {
                std::cout << "[InternalClient] DeviceServer disconnected. Reconnecting..." << std::endl;
                break;
            }
        }

        // 소켓 fd 리셋
        {
            std::lock_guard<std::mutex> lock(fd_mutex_);
            current_fd_ = -1;
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

    // Push 이벤트 디스패치
    dispatchPushEvent(header, buf, sock_fd);
    return true;
}

// ======================== Push 이벤트 디스패치 ========================

void InternalClient::dispatchPushEvent(const PacketHeader& header, const std::vector<char>& body_buf, int sock_fd) {
    std::string body_str(body_buf.begin(), body_buf.end());

    // CAMERA Push → 캐시 갱신
    if (header.type == MessageType::CAMERA) {
        try {
            json cameras = json::parse(body_str);
            std::lock_guard<std::mutex> lock(cache_mutex_);
            cached_cameras_ = cameras;
            std::cout << "[InternalClient] CAMERA cache updated (" << cameras.size() << " items)" << std::endl;
        } catch (const json::parse_error& e) {
            std::cerr << "[InternalClient] CAMERA JSON parse error: " << e.what() << std::endl;
        }
        return;
    }

    // AVAILABLE Push → 캐시 갱신
    if (header.type == MessageType::AVAILABLE) {
        try {
            json status = json::parse(body_str);
            std::lock_guard<std::mutex> lock(cache_mutex_);
            cached_device_status_ = status;
        } catch (const json::parse_error& e) {
            std::cerr << "[InternalClient] AVAILABLE JSON parse error: " << e.what() << std::endl;
        }
        return;
    }

    // AI 이벤트
    if (header.type == MessageType::AI) {
        try {
            json event = json::parse(body_str);
            if (on_ai_event_) on_ai_event_(event);
        } catch (const json::parse_error& e) {
            std::cerr << "[InternalClient] AI JSON parse error: " << e.what() << std::endl;
        }
    }
    // IMAGE 이벤트
    else if (header.type == MessageType::IMAGE) {
        try {
            json meta = json::parse(body_str);
            uint32_t jpeg_size = meta.value("jpeg_size", 0);
            if (jpeg_size > 0 && jpeg_size < 10 * 1024 * 1024) {
                std::vector<char> jpeg(jpeg_size);
                if (recvExact(sock_fd, jpeg.data(), jpeg_size)) {
                    if (on_image_event_) on_image_event_(meta, jpeg);
                }
            }
        } catch (const json::parse_error& e) {
            std::cerr << "[InternalClient] IMAGE JSON parse error: " << e.what() << std::endl;
        }
    }
    // META 센서 데이터
    else if (header.type == MessageType::META) {
        try {
            json sensor_data = json::parse(body_str);
            if (on_meta_event_) on_meta_event_(sensor_data);
        } catch (const json::parse_error& e) {
            std::cerr << "[InternalClient] META JSON parse error: " << e.what() << std::endl;
        }
    }
}

bool InternalClient::sendMessage(int fd, MessageType type, const json& body) {
    std::string body_str = body.dump();
    PacketHeader header;
    header.type = type;
    header.body_length = htonl(static_cast<uint32_t>(body_str.size()));

    if (!sendExact(fd, &header, sizeof(header))) return false;
    if (!body_str.empty()) {
        if (!sendExact(fd, body_str.c_str(), body_str.size())) return false;
    }
    return true;
}

// recvExact: NetUtil.h 공통 함수 사용
