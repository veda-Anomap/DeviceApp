#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <algorithm>

#include "QtCommServer.h"
#include "NetUtil.h"

// ======================== 생성자 / 소멸자 ========================

QtCommServer::QtCommServer() {
    std::cout << "[QtComm] Server created." << std::endl;
}

QtCommServer::~QtCommServer() {
    stop();
    if (tls_ctx_) {
        SSL_CTX_free(tls_ctx_);
        tls_ctx_ = nullptr;
    }
}

// ======================== TLS 초기화 ========================

bool QtCommServer::initTLS(const std::string& cert_path, const std::string& key_path, const std::string& ca_path) {
    tls_ctx_ = TLS::ServerContext(cert_path.c_str(), key_path.c_str(), ca_path.c_str());
    if (!tls_ctx_) {
        std::cerr << "[QtComm] TLS context 초기화 실패" << std::endl;
        return false;
    }
    std::cout << "[QtComm] TLS initialized (mTLS enabled)" << std::endl;
    return true;
}

// ======================== Raw 패킷 전송 ========================

bool QtCommServer::sendRaw(int client_fd, MessageType type, const std::vector<uint8_t>& data) {
    PacketHeader header;
    header.type = type;
    header.body_length = static_cast<uint32_t>(data.size());

    if (!sendExact(client_fd, &header, sizeof(header))) return false;
    if (!data.empty()) {
        if (!sendExact(client_fd, data.data(), data.size())) return false;
    }
    return true;
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

    // TLS 세션 정리
    {
        std::lock_guard<std::mutex> lock(tls_mutex_);
        tls_sessions_.clear();
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

    // TLS 세션 생성 (tls_ctx_가 있으면 보안 모드)
    bool tls_enabled = (tls_ctx_ != nullptr);
    if (tls_enabled) {
        std::lock_guard<std::mutex> lock(tls_mutex_);
        tls_sessions_.emplace(client_fd, TLS::Session(tls_ctx_, true));

        auto it = tls_sessions_.find(client_fd);
        if (it == tls_sessions_.end() || !it->second.isValid()) {
            std::cerr << "[QtComm] TLS session creation failed (fd: " << client_fd << ")" << std::endl;
            close(client_fd);
            return;
        }
    }

    while (is_running_) {
        // 1. 헤더 수신 (5바이트: Type 1 + BodyLength 4)
        PacketHeader header;
        if (!recvExact(client_fd, &header, sizeof(header))) {
            std::cout << "[QtComm] Client disconnected (fd: " << client_fd << ")" << std::endl;
            break;
        }

        uint32_t body_len = header.body_length;

        // 안전 검사: 비정상적으로 큰 패킷 방지 (최대 1MB)
        if (body_len > 1024 * 1024) {
            std::cerr << "[QtComm] Body too large: " << body_len << " bytes. Disconnecting." << std::endl;
            break;
        }

        // 2. Body 수신
        std::vector<char> buf(body_len);
        if (body_len > 0) {
            if (!recvExact(client_fd, buf.data(), body_len)) {
                std::cout << "[QtComm] Failed to receive body (fd: " << client_fd << ")" << std::endl;
                break;
            }
        }

        // ===================== TLS 핸드셰이크 처리 =====================
        if (tls_enabled) {
            bool is_handshake = false;
            bool handshake_done = false;
            bool handshake_rejected = false;
            std::vector<uint8_t> handshake_resp;
            std::vector<uint8_t> plain;

            // mutex 범위: decrypt/getHandshakeData만 보호
            {
                std::lock_guard<std::mutex> lock(tls_mutex_);
                auto it = tls_sessions_.find(client_fd);
                if (it == tls_sessions_.end()) break;
                auto& session = it->second;

                if (!session.isHandshakeDone()) {
                    is_handshake = true;
                    if (header.type != MessageType::HANDSHAKE) {
                        std::cerr << "[QtComm] 핸드셰이크 완료 전 비허용 타입 수신 (fd: " << client_fd << ")" << std::endl;
                        handshake_rejected = true;
                    } else {
                        session.decrypt(buf.data(), body_len);
                        handshake_resp = session.getHandshakeData();
                        handshake_done = session.isHandshakeDone();
                    }
                } else {
                    plain = session.decrypt(buf.data(), body_len);
                }
            }
            // mutex 해제됨 — 이후 sendRaw/on_message_ 안전

            if (handshake_rejected) break;

            if (is_handshake) {
                if (!handshake_resp.empty()) {
                    sendRaw(client_fd, MessageType::HANDSHAKE, handshake_resp);
                }
                if (handshake_done) {
                    std::cout << "[QtComm] TLS handshake completed (fd: " << client_fd << ")" << std::endl;
                }
                continue;
            }

            // 암호화 통신
            if (plain.empty()) {
                continue;
            }

            json body = json::object();
            try {
                body = json::parse(std::string(plain.begin(), plain.end()));
            } catch (const json::parse_error& e) {
                std::cerr << "[QtComm] JSON parse error (decrypted): " << e.what() << std::endl;
                sendMessage(client_fd, MessageType::FAIL, {{"error", "JSON parse error"}});
                continue;
            }

            std::cout << "[QtComm] Received (TLS): Type=0x" << std::hex
                      << static_cast<int>(header.type) << std::dec
                      << " Body=" << body.dump() << std::endl;

            if (on_message_) {
                on_message_(client_fd, header.type, body);
            }
        } else {
            // ===================== 평문 모드 (TLS 미사용) =====================
            json body = json::object();
            if (body_len > 0) {
                try {
                    body = json::parse(std::string(buf.begin(), buf.end()));
                } catch (const json::parse_error& e) {
                    std::cerr << "[QtComm] JSON parse error: " << e.what() << std::endl;
                    sendMessage(client_fd, MessageType::FAIL, {{"error", "JSON parse error"}});
                    continue;
                }
            }

            std::cout << "[QtComm] Received: Type=0x" << std::hex
                      << static_cast<int>(header.type) << std::dec
                      << " Body=" << body.dump() << std::endl;

            if (on_message_) {
                on_message_(client_fd, header.type, body);
            }
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
        client_roles_.erase(client_fd);
    }

    // TLS 세션 정리
    {
        std::lock_guard<std::mutex> lock(tls_mutex_);
        tls_sessions_.erase(client_fd);
    }

    // "나 끝났어" 표시 -> acceptLoop의 cleanupFinishedThreads()가 수거함
    {
        std::lock_guard<std::mutex> lock(finished_mutex_);
        finished_fds_.insert(client_fd);
    }

    std::cout << "[QtComm] Handler ended for fd: " << client_fd << std::endl;
}

// ======================== 메시지 전송 ========================

bool QtCommServer::sendMessage(int client_fd, MessageType type, const json& body) {
    std::string body_str = body.dump();

    // TLS 암호화 전송
    {
        std::lock_guard<std::mutex> lock(tls_mutex_);
        auto it = tls_sessions_.find(client_fd);
        if (it != tls_sessions_.end() && it->second.isHandshakeDone()) {
            auto cipher = it->second.encrypt(body_str.c_str(), body_str.size());
            if (cipher.empty()) {
                std::cerr << "[QtComm] Encrypt failed (fd: " << client_fd << ")" << std::endl;
                return false;
            }

            PacketHeader header;
            header.type = type;
            header.body_length = static_cast<uint32_t>(cipher.size());

            if (!sendExact(client_fd, &header, sizeof(header))) return false;
            if (!sendExact(client_fd, cipher.data(), cipher.size())) return false;
            return true;
        }
    }

    // 평문 전송 (TLS 미사용 or 핸드셰이크 미완료)
    PacketHeader header;
    header.type = type;
    header.body_length = static_cast<uint32_t>(body_str.size());

    if (!sendExact(client_fd, &header, sizeof(header))) {
        std::cerr << "[QtComm] Send header failed (fd: " << client_fd << ")" << std::endl;
        return false;
    }

    if (!body_str.empty()) {
        if (!sendExact(client_fd, body_str.c_str(), body_str.size())) {
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

void QtCommServer::broadcastToRole(MessageType type, const json& body, const std::string& min_role) {
    std::lock_guard<std::mutex> lock(client_mutex_);
    for (int fd : client_fds_) {
        auto it = client_roles_.find(fd);
        if (it == client_roles_.end() || it->second.empty()) continue; // 미인증

        const std::string& role = it->second;
        if (min_role == "user" && (role == "user" || role == "admin")) {
            sendMessage(fd, type, body);
        } else if (min_role == "admin" && role == "admin") {
            sendMessage(fd, type, body);
        }
    }
}

void QtCommServer::broadcastByRole(MessageType type, const json& user_body, const json& admin_body) {
    std::lock_guard<std::mutex> lock(client_mutex_);
    for (int fd : client_fds_) {
        auto it = client_roles_.find(fd);
        if (it == client_roles_.end() || it->second.empty()) continue;

        if (it->second == "admin") {
            sendMessage(fd, type, admin_body);
        } else if (it->second == "user") {
            sendMessage(fd, type, user_body);
        }
    }
}

void QtCommServer::setClientRole(int client_fd, const std::string& role) {
    std::lock_guard<std::mutex> lock(client_mutex_);
    client_roles_[client_fd] = role;
    std::cout << "[QtComm] Client fd:" << client_fd << " role set to '" << role << "'" << std::endl;
}

int QtCommServer::getClientCount() {
    std::lock_guard<std::mutex> lock(client_mutex_);
    return static_cast<int>(client_fds_.size());
}

// ======================== 헬퍼 함수 ========================

// recvExact: NetUtil.h 공통 함수 사용

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

// ======================== IMAGE 전송 ========================

bool QtCommServer::sendImageMessage(int client_fd, const json& meta, const std::vector<char>& jpeg) {
    std::string meta_str = meta.dump();

    // TLS: JSON 메타만 암호화, JPEG는 평문
    {
        std::lock_guard<std::mutex> lock(tls_mutex_);
        auto it = tls_sessions_.find(client_fd);
        if (it != tls_sessions_.end() && it->second.isHandshakeDone()) {
            auto cipher_meta = it->second.encrypt(meta_str.c_str(), meta_str.size());
            if (cipher_meta.empty()) return false;

            PacketHeader header;
            header.type = MessageType::IMAGE;
            header.body_length = static_cast<uint32_t>(cipher_meta.size());

            if (!sendExact(client_fd, &header, sizeof(header))) return false;
            if (!sendExact(client_fd, cipher_meta.data(), cipher_meta.size())) return false;
            if (!jpeg.empty()) {
                if (!sendExact(client_fd, jpeg.data(), jpeg.size())) return false;
            }
            return true;
        }
    }

    // 평문 모드
    PacketHeader header;
    header.type = MessageType::IMAGE;
    header.body_length = static_cast<uint32_t>(meta_str.size());

    if (!sendExact(client_fd, &header, sizeof(header))) return false;
    if (!sendExact(client_fd, meta_str.c_str(), meta_str.size())) return false;
    if (!jpeg.empty()) {
        if (!sendExact(client_fd, jpeg.data(), jpeg.size())) return false;
    }

    return true;
}

void QtCommServer::broadcastImage(const json& meta, const std::vector<char>& jpeg) {
    std::lock_guard<std::mutex> lock(client_mutex_);
    for (int fd : client_fds_) {
        // role이 설정된 (로그인된) 클라이언트에게만 전송
        auto role_it = client_roles_.find(fd);
        if (role_it != client_roles_.end() && !role_it->second.empty()) {
            sendImageMessage(fd, meta, jpeg);
        }
    }
}
