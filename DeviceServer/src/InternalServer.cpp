#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>

#include "InternalServer.h"

InternalServer::InternalServer() {}

InternalServer::~InternalServer() {
    stop();
}

void InternalServer::start(int port, CameraListProvider provider) {
    if (is_running_) return;

    port_ = port;
    get_camera_list_ = provider;

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

    if (send(client_fd, &header, sizeof(header), MSG_NOSIGNAL) < 0) return false;
    if (!body_str.empty()) {
        if (send(client_fd, body_str.c_str(), body_str.size(), MSG_NOSIGNAL) < 0) return false;
    }
    return true;
}

bool InternalServer::recvExact(int fd, void* buf, size_t len) {
    size_t received = 0;
    char* ptr = static_cast<char*>(buf);
    while (received < len) {
        ssize_t n = recv(fd, ptr + received, len - received, 0);
        if (n <= 0) return false;
        received += n;
    }
    return true;
}

void InternalServer::broadcastAiEvent(const json& event) {
    std::lock_guard<std::mutex> lock(client_mutex_);
    for (int fd : client_fds_) {
        sendMessage(fd, MessageType::AI, event);
    }
    std::cout << "[Internal] Broadcasted AI event to " << client_fds_.size() << " client(s)." << std::endl;
}
