#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>

#include "InternalServer.h"
#include "NetUtil.h"

InternalServer::InternalServer() {}

InternalServer::~InternalServer() {
    stop();
}

void InternalServer::start(int port, CameraListProvider camera_provider, DeviceStatusProvider status_provider) {
    if (is_running_) return;

    port_ = port;
    get_camera_list_ = camera_provider;
    get_device_status_ = status_provider;

    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        std::cerr << "[Internal] Socket creation failed: " << strerror(errno) << std::endl;
        return;
    }

    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "[Internal] Bind failed on port " << port_ << ": " << strerror(errno) << std::endl;
        close(server_fd_);
        server_fd_ = -1;
        return;
    }

    if (listen(server_fd_, 3) < 0) {
        std::cerr << "[Internal] Listen failed: " << strerror(errno) << std::endl;
        close(server_fd_);
        server_fd_ = -1;
        return;
    }

    is_running_ = true;
    accept_thread_ = std::thread(&InternalServer::acceptLoop, this);

    std::cout << "[Internal] Server started on port " << port_ << std::endl;
}

void InternalServer::stop() {
    if (!is_running_) return;
    is_running_ = false;

    if (server_fd_ != -1) {
        shutdown(server_fd_, SHUT_RDWR);
        close(server_fd_);
        server_fd_ = -1;
    }

    {
        std::lock_guard<std::mutex> lock(client_mutex_);
        for (int fd : client_fds_) {
            shutdown(fd, SHUT_RDWR);
            close(fd);
        }
        client_fds_.clear();
    }

    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }

    std::cout << "[Internal] Server stopped." << std::endl;
}

void InternalServer::acceptLoop() {
    while (is_running_) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &addr_len);
        if (client_fd < 0) {
            if (is_running_) {
                std::cerr << "[Internal] Accept error: " << strerror(errno) << std::endl;
            }
            break;
        }

        std::cout << "[Internal] StreamServer connected (fd: " << client_fd << ")" << std::endl;

        {
            std::lock_guard<std::mutex> lock(client_mutex_);
            client_fds_.push_back(client_fd);
        }

        // 클라이언트 핸들러 (detach — 내부 통신은 간단하므로)
        std::thread(&InternalServer::clientHandler, this, client_fd).detach();
    }
}

void InternalServer::clientHandler(int client_fd) {
    while (is_running_) {
        // 헤더 수신
        PacketHeader header;
        if (!recvExact(client_fd, &header, sizeof(header))) {
            std::cout << "[Internal] Client disconnected (fd: " << client_fd << ")" << std::endl;
            break;
        }

        uint32_t body_len = ntohl(header.body_length);

        // body 수신 (있으면)
        json body = json::object();
        if (body_len > 0 && body_len < 1024 * 1024) {
            std::vector<char> buf(body_len);
            if (!recvExact(client_fd, buf.data(), body_len)) break;
            try { body = json::parse(std::string(buf.begin(), buf.end())); }
            catch (...) { continue; }
        }

        // CAMERA 요청 처리
        if (header.type == MessageType::CAMERA) {
            json camera_list = get_camera_list_();
            sendMessage(client_fd, MessageType::CAMERA, camera_list);
            std::cout << "[Internal] Sent camera list to StreamServer." << std::endl;
        }
        // AVAILABLE 요청 처리 (디바이스 상태)
        else if (header.type == MessageType::AVAILABLE) {
            json device_status = get_device_status_();
            sendMessage(client_fd, MessageType::AVAILABLE, device_status);
            std::cout << "[Internal] Sent device status to StreamServer." << std::endl;
        }
        // DEVICE 명령 처리 (모터 제어 → Sub-Pi로 전달)
        else if (header.type == MessageType::DEVICE) {
            std::cout << "[Internal] DEVICE command received: " << body.dump() << std::endl;
            if (on_device_command_) {
                on_device_command_(body);
            }
        }
    }

    close(client_fd);
    {
        std::lock_guard<std::mutex> lock(client_mutex_);
        client_fds_.erase(
            std::remove(client_fds_.begin(), client_fds_.end(), client_fd),
            client_fds_.end()
        );
    }
}

bool InternalServer::sendMessage(int client_fd, MessageType type, const json& body) {
    std::string body_str = body.dump();
    PacketHeader header;
    header.type = type;
    header.body_length = htonl(static_cast<uint32_t>(body_str.size()));

    if (!sendExact(client_fd, &header, sizeof(header))) return false;
    if (!body_str.empty()) {
        if (!sendExact(client_fd, body_str.c_str(), body_str.size())) return false;
    }
    return true;
}

// recvExact: NetUtil.h 공통 함수 사용

void InternalServer::broadcastAiEvent(const json& event) {
    std::lock_guard<std::mutex> lock(client_mutex_);
    for (int fd : client_fds_) {
        sendMessage(fd, MessageType::AI, event);
    }
    std::cout << "[Internal] Broadcasted AI event to " << client_fds_.size() << " client(s)." << std::endl;
}

void InternalServer::broadcastMetaEvent(const json& sensor_data) {
    std::lock_guard<std::mutex> lock(client_mutex_);
    for (int fd : client_fds_) {
        sendMessage(fd, MessageType::META, sensor_data);
    }
}

void InternalServer::broadcastImageEvent(const json& meta, const std::vector<char>& jpeg) {
    std::lock_guard<std::mutex> lock(client_mutex_);
    for (int fd : client_fds_) {
        sendImageMessage(fd, meta, jpeg);
    }
    std::cout << "[Internal] Broadcasted IMAGE event to " << client_fds_.size() << " client(s)." << std::endl;
}

bool InternalServer::sendImageMessage(int client_fd, const json& meta, const std::vector<char>& jpeg) {
    std::string meta_str = meta.dump();

    // 헤더: Type=IMAGE, BodyLength=JSON 길이
    PacketHeader header;
    header.type = MessageType::IMAGE;
    header.body_length = htonl(static_cast<uint32_t>(meta_str.size()));

    // 1. 헤더 전송
    if (!sendExact(client_fd, &header, sizeof(header))) return false;
    // 2. JSON 메타데이터 전송
    if (!sendExact(client_fd, meta_str.c_str(), meta_str.size())) return false;
    // 3. JPEG 바이너리 전송
    if (!jpeg.empty()) {
        if (!sendExact(client_fd, jpeg.data(), jpeg.size())) return false;
    }

    return true;
}
