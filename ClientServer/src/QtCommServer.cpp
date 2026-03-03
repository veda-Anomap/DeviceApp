#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <algorithm>

#include "QtCommServer.h"

// ======================== 생성자 / 소멸자 ========================

QtCommServer::QtCommServer() {
    std::cout << "[QtComm] Server created." << std::endl;
}

QtCommServer::~QtCommServer() {
    stop();
}

// ======================== 서버 시작 / 종료 ========================

void QtCommServer::start(int port, QtMessageCallback callback) {
    if (is_running_) return;

    port_ = port;
    on_message_ = callback;

    // 1. TCP 소켓 생성
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        std::cerr << "[QtComm] Socket creation failed: " << strerror(errno) << std::endl;
        return;
    }

    // SO_REUSEADDR: 서버 재시작 시 "Address already in use" 방지
    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 2. 바인딩
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "[QtComm] Bind failed on port " << port_ << ": " << strerror(errno) << std::endl;
        close(server_fd_);
        server_fd_ = -1;
        return;
    }

    // 3. 리슨 (최대 대기 큐: 5)
    if (listen(server_fd_, 5) < 0) {
        std::cerr << "[QtComm] Listen failed: " << strerror(errno) << std::endl;
        close(server_fd_);
        server_fd_ = -1;
        return;
    }

    is_running_ = true;
    accept_thread_ = std::thread(&QtCommServer::acceptLoop, this);

    std::cout << "[QtComm] Server started on port " << port_ << std::endl;
}

void QtCommServer::stop() {
    if (!is_running_) return;
    is_running_ = false;

    // 서버 소켓 닫기 -> accept()가 에러로 리턴되면서 스레드 탈출
    if (server_fd_ != -1) {
        shutdown(server_fd_, SHUT_RDWR);
        close(server_fd_);
        server_fd_ = -1;
    }

    // 연결된 클라이언트 소켓 모두 닫기
    {
        std::lock_guard<std::mutex> lock(client_mutex_);
        for (int fd : client_fds_) {
            shutdown(fd, SHUT_RDWR);
            close(fd);
        }
        client_fds_.clear();
    }

    // 스레드 정리
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }
    for (auto& [fd, t] : client_threads_) {
        if (t.joinable()) {
            t.join();
        }
    }
    client_threads_.clear();

    std::cout << "[QtComm] Server stopped." << std::endl;
}

// ======================== 클라이언트 수락 루프 ========================

void QtCommServer::acceptLoop() {
    std::cout << "[QtComm] Accept loop started. Waiting for Qt clients..." << std::endl;

    while (is_running_) {
        // [추가] 새 클라이언트를 받기 전에, 끝난 스레드부터 정리
        cleanupFinishedThreads();

        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &addr_len);
        if (client_fd < 0) {
            if (is_running_) {
                std::cerr << "[QtComm] Accept error: " << strerror(errno) << std::endl;
            }
            break; // 서버 종료 시 탈출
        }

        std::string client_ip = inet_ntoa(client_addr.sin_addr);
        std::cout << "[QtComm] Client connected: " << client_ip 
                  << " (fd: " << client_fd << ")" << std::endl;

        // 클라이언트 목록에 추가
        {
            std::lock_guard<std::mutex> lock(client_mutex_);
            client_fds_.push_back(client_fd);
        }

        // 클라이언트 전용 핸들러 스레드 생성 (fd → thread 매핑)
        client_threads_[client_fd] = std::thread(&QtCommServer::clientHandler, this, client_fd);
    }
}

// ======================== 클라이언트 핸들러 ========================

void QtCommServer::clientHandler(int client_fd) {
    std::cout << "[QtComm] Handler started for fd: " << client_fd << std::endl;

    while (is_running_) {
        // 1. 헤더 수신 (5바이트: Type 1 + BodyLength 4)
        PacketHeader header;
        if (!recvExact(client_fd, &header, sizeof(header))) {
            std::cout << "[QtComm] Client disconnected (fd: " << client_fd << ")" << std::endl;
            break;
        }

        // 네트워크 바이트 오더 -> 호스트 바이트 오더
        uint32_t body_len = ntohl(header.body_length);

        // 안전 검사: 비정상적으로 큰 패킷 방지 (최대 1MB)
        if (body_len > 1024 * 1024) {
            std::cerr << "[QtComm] Body too large: " << body_len << " bytes. Disconnecting." << std::endl;
            break;
        }

        // 2. JSON 본문 수신
        json body = json::object(); // 기본값: 빈 JSON 객체

        if (body_len > 0) {
            std::vector<char> buf(body_len);
            if (!recvExact(client_fd, buf.data(), body_len)) {
                std::cout << "[QtComm] Failed to receive body (fd: " << client_fd << ")" << std::endl;
                break;
            }

            try {
                body = json::parse(std::string(buf.begin(), buf.end()));
            } catch (const json::parse_error& e) {
                std::cerr << "[QtComm] JSON parse error: " << e.what() << std::endl;
                // 파싱 실패해도 연결은 유지 (FAIL 응답 보내기)
                sendMessage(client_fd, MessageType::FAIL, {{"error", "JSON parse error"}});
                continue;
            }
        }

        std::cout << "[QtComm] Received: Type=0x" << std::hex 
                  << static_cast<int>(header.type) << std::dec
                  << " Body=" << body.dump() << std::endl;

        // 3. 콜백 호출 (AppController에서 처리)
        if (on_message_) {
            on_message_(client_fd, header.type, body);
        }
    }

    // 클라이언트 정리
    close(client_fd);
    {
        std::lock_guard<std::mutex> lock(client_mutex_);
        client_fds_.erase(
            std::remove(client_fds_.begin(), client_fds_.end(), client_fd),
            client_fds_.end()
        );
    }

    // [추가] "나 끝났어" 표시 -> acceptLoop의 cleanupFinishedThreads()가 수거함
    {
        std::lock_guard<std::mutex> lock(finished_mutex_);
        finished_fds_.insert(client_fd);
    }

    std::cout << "[QtComm] Handler ended for fd: " << client_fd << std::endl;
}

// ======================== 메시지 전송 ========================

bool QtCommServer::sendMessage(int client_fd, MessageType type, const json& body) {
    std::string body_str = body.dump();

    // 헤더 구성
    PacketHeader header;
    header.type = type;
    header.body_length = htonl(static_cast<uint32_t>(body_str.size()));

    // 헤더 전송
    if (send(client_fd, &header, sizeof(header), MSG_NOSIGNAL) < 0) {
        std::cerr << "[QtComm] Send header failed (fd: " << client_fd << ")" << std::endl;
        return false;
    }

    // 본문 전송
    if (!body_str.empty()) {
        if (send(client_fd, body_str.c_str(), body_str.size(), MSG_NOSIGNAL) < 0) {
            std::cerr << "[QtComm] Send body failed (fd: " << client_fd << ")" << std::endl;
            return false;
        }
    }

    return true;
}

void QtCommServer::broadcast(MessageType type, const json& body) {
    std::lock_guard<std::mutex> lock(client_mutex_);
    for (int fd : client_fds_) {
        sendMessage(fd, type, body);
    }
}

int QtCommServer::getClientCount() {
    std::lock_guard<std::mutex> lock(client_mutex_);
    return static_cast<int>(client_fds_.size());
}

// ======================== 헬퍼 함수 ========================

bool QtCommServer::recvExact(int fd, void* buf, size_t len) {
    size_t received = 0;
    char* ptr = static_cast<char*>(buf);

    while (received < len) {
        ssize_t n = recv(fd, ptr + received, len - received, 0);
        if (n <= 0) {
            return false;
        }
        received += n;
    }
    return true;
}

// ======================== 스레드 정리 ========================

void QtCommServer::cleanupFinishedThreads() {
    // 1단계: 끝난 fd 목록을 복사 후 뮤텍스 해제 (데드락 방지)
    std::set<int> fds_to_clean;
    {
        std::lock_guard<std::mutex> lock(finished_mutex_);
        if (finished_fds_.empty()) return;
        fds_to_clean.swap(finished_fds_);
    }
    // finished_mutex_ 해제됨 — clientHandler가 finished_fds_에 접근 가능

    // 2단계: 해당 fd의 스레드만 join 후 제거
    for (int fd : fds_to_clean) {
        auto it = client_threads_.find(fd);
        if (it != client_threads_.end()) {
            if (it->second.joinable()) {
                it->second.join();
            }
            client_threads_.erase(it);
        }
    }

    std::cout << "[QtComm] Cleaned up " << fds_to_clean.size()
              << " finished thread(s)." << std::endl;
}
