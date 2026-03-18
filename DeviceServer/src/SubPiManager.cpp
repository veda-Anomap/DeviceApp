#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <sys/select.h>

#include "SubPiManager.h"
#include "NetUtil.h"

SubPiManager::SubPiManager() {}

SubPiManager::~SubPiManager() {
    stop();
}

void SubPiManager::start(std::atomic<bool>& is_running) {
    is_running_ = &is_running;
    beacon_thread_ = std::thread(&SubPiManager::runBeaconReceiver, this);
}

void SubPiManager::stop() {
    // 모든 리스너 스레드 join
    for (auto& pair : listener_threads_) {
        if (pair.second.joinable()) {
            pair.second.join();
        }
    }
    listener_threads_.clear();
    if (beacon_thread_.joinable()) beacon_thread_.join();
}

// ======================== 비콘 수신 ========================

void SubPiManager::runBeaconReceiver() {
    std::cout << "[DEBUG] BeaconReceiver Thread: STARTING..." << std::endl;
    int sockfd;
    struct sockaddr_in servaddr, cliaddr;

    // 1. UDP 소켓 생성
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("[ERROR] Beacon: Socket creation failed");
        return;
    }
    std::cout << "[DEBUG] BeaconReceiver: Socket Created (fd: " << sockfd << ")" << std::endl;

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(10001);

    // 2. 포트 바인딩
    int bind_result = bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr));
    if (bind_result < 0) {
        perror("[ERROR] Beacon: Bind failed");
        close(sockfd);
        return;
    }
    std::cout << "[DEBUG] BeaconReceiver: Bind SUCCESS! Listening on 10001..." << std::endl;

    char buffer[1024];
    while (*is_running_) {
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        socklen_t len = sizeof(cliaddr);
        int n = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0, (struct sockaddr *)&cliaddr, &len);

        if (n > 0) {
            buffer[n] = '\0';
            std::string sender_ip = inet_ntoa(cliaddr.sin_addr);
            std::string message(buffer);

            if (message.find("SUB_PI_ALIVE") != std::string::npos) {
                std::string id = "SubPi_" + sender_ip;

                // 이미 등록된 + 온라인인 장치인지 확인
                if (is_registered_ && is_registered_(id)) {
                    continue;  // 이미 온라인 → 스킵
                }

                int tcp_socket;

                // TCP로 스트리밍 시작 명령 전송
                if (requestStartStream(sender_ip, next_port_, tcp_socket)) {
                    DeviceInfo info;
                    info.id = id;
                    info.ip = sender_ip;
                    info.type = DeviceType::SUB_PI;
                    info.udp_listen_port = next_port_++;
                    info.command_socket_fd = tcp_socket;
                    info.is_online = true;

                    // 콜백으로 DeviceManager에 알림 (신규 등록 또는 오프라인 복구)
                    if (on_device_found_) {
                        on_device_found_(info, tcp_socket);
                    }

                    // AI 이벤트 리스너 스레드 생성 (종료된 스레드 정리 후)
                    cleanupFinishedThreads();

                    // 기존 스레드가 있으면 join 후 교체
                    auto it = listener_threads_.find(id);
                    if (it != listener_threads_.end()) {
                        if (it->second.joinable()) {
                            it->second.join();
                        }
                        listener_threads_.erase(it);
                    }
                    listener_threads_[id] = std::thread(&SubPiManager::subPiListener, this, id, tcp_socket);
                }
            }
        }
    }

    close(sockfd);
    std::cout << "Beacon: Thread stopped." << std::endl;
}

// ======================== TCP 스트림 시작 요청 ========================

bool SubPiManager::requestStartStream(const std::string& target_ip, int listen_port, int& tcp_socket) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;
    tcp_socket = sock;

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(5000);
    inet_pton(AF_INET, target_ip.c_str(), &serv_addr.sin_addr);

    struct timeval timeout = {2, 0};
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cerr << "[TCP] Connection failed to " << target_ip << std::endl;
        close(sock);
        return false;
    }

    std::string msg = "START_STREAM:" + std::to_string(listen_port);
    send(sock, msg.c_str(), msg.size(), 0);

    std::cout << "[TCP] Sent command to " << target_ip << " : " << msg << std::endl;

    return true;
}

// ======================== AI 이벤트 리스너 ========================

void SubPiManager::subPiListener(std::string device_id, int socket_fd) {
    std::cout << "[AI Listener] Started for " << device_id << " (fd: " << socket_fd << ")" << std::endl;

    int error_count = 0;

    while (*is_running_) {
        // select로 30초 대기 — 데이터 있으면 읽고, 없으면 루프 재시작
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(socket_fd, &read_fds);

        struct timeval tv;
        tv.tv_sec = 30;
        tv.tv_usec = 0;

        int ret = select(socket_fd + 1, &read_fds, nullptr, nullptr, &tv);
        if (ret < 0) {
            std::cerr << "[AI Listener] select error for " << device_id << std::endl;
            break;
        }
        if (ret == 0) continue;  // 30초 동안 데이터 없음 → 정상, 루프 계속

        // 1. 헤더 수신 (5바이트: Type + BodyLength)
        PacketHeader header;
        if (!recvExact(socket_fd, &header, sizeof(header))) {
            std::cout << "[AI Listener] " << device_id << " disconnected." << std::endl;
            break;
        }

        uint32_t body_len = ntohl(header.body_length);

        // [디버그] 헤더 로그
        std::cout << "[AI Listener] Header: type=0x" << std::hex 
                  << static_cast<int>(header.type) << std::dec 
                  << " body_len=" << body_len << std::endl;

        // 비정상 패킷 → 스킵
        if (body_len == 0 || body_len > 1024 * 1024) {
            std::cerr << "[AI Listener] Invalid body_len: " << body_len 
                      << " from " << device_id << ". Skipping." << std::endl;
            continue;
        }

        // 2. JSON 본문 수신
        std::vector<char> buf(body_len);
        if (!recvExact(socket_fd, buf.data(), body_len)) break;

        std::string body_str(buf.begin(), buf.end());

        // [디버그] 본문 앞 50자 출력
        std::cout << "[AI Listener] Body(" << body_len << "): " 
                  << body_str.substr(0, 50) << std::endl;

        // 3. AI 타입인 경우 콜백 호출
        if (header.type == MessageType::AI) {
            try {
                json event = json::parse(body_str);
                event["device_id"] = device_id;
                
                // IP 추출 및 추가 ("SubPi_192.168.0.43" -> "192.168.0.43")
                std::string ip = device_id.substr(6);
                event["ip"] = ip;

                std::cout << "[AI Listener] Event from " << device_id << ": " << event.dump() << std::endl;

                if (on_ai_event_) {
                    on_ai_event_(device_id, event);
                }
                error_count = 0;
            } catch (const json::parse_error& e) {
                std::cerr << "[AI Listener] JSON parse error: " << e.what() << std::endl;
                error_count++;
                if (error_count >= 3) {
                    std::cerr << "[AI Listener] Too many errors. Closing " << device_id << std::endl;
                    break;
                }
            }
        }
        // 4. IMAGE 타입인 경우: JSON 메타 + JPEG 바이너리 수신
        else if (header.type == MessageType::IMAGE) {
            try {
                json meta = json::parse(body_str);
                meta["device_id"] = device_id;
                
                // IP 추출 및 추가 ("SubPi_192.168.0.43" -> "192.168.0.43")
                std::string ip = device_id.substr(6);
                meta["ip"] = ip;

                uint32_t jpeg_size = meta.value("jpeg_size", 0u);
                if (jpeg_size == 0 || jpeg_size > 10 * 1024 * 1024) { // 최대 10MB
                    std::cerr << "[AI Listener] Invalid jpeg_size: " << jpeg_size << std::endl;
                    continue;
                }

                // JPEG 바이너리 수신
                std::vector<char> jpeg_buf(jpeg_size);
                if (!recvExact(socket_fd, jpeg_buf.data(), jpeg_size)) {
                    std::cerr << "[AI Listener] Failed to receive JPEG from " << device_id << std::endl;
                    break;
                }

                std::cout << "[AI Listener] IMAGE from " << device_id 
                          << ": frame " << meta.value("frame_index", -1) 
                          << "/" << meta.value("total_frames", -1)
                          << " (" << jpeg_size << " bytes)" << std::endl;

                if (on_image_event_) {
                    on_image_event_(device_id, meta, jpeg_buf);
                }
                error_count = 0;
            } catch (const json::parse_error& e) {
                std::cerr << "[AI Listener] IMAGE JSON parse error: " << e.what() << std::endl;
                error_count++;
                if (error_count >= 3) break;
            }
        }
        // 5. AVAILABLE 타입인 경우: SubCam 시스템 상태 수신
        else if (header.type == MessageType::AVAILABLE) {
            try {
                json status = json::parse(body_str);

                std::cout << "[AI Listener] AVAILABLE from " << device_id 
                          << ": cpu=" << status.value("cpu", 0.0)
                          << " mem=" << status.value("memory", 0.0)
                          << " temp=" << status.value("temp", 0.0) << std::endl;

                if (on_available_event_) {
                    on_available_event_(device_id, status);
                }
                error_count = 0;
            } catch (const json::parse_error& e) {
                std::cerr << "[AI Listener] AVAILABLE JSON parse error: " << e.what() << std::endl;
                error_count++;
                if (error_count >= 3) break;
            }
        }
        // 6. META 타입인 경우: 센서 배치 데이터 수신 → 즉시 전달
        else if (header.type == MessageType::META) {
            try {
                json sensor_data = json::parse(body_str);
                sensor_data["device_id"] = device_id;
                
                // IP 추출 및 추가 ("SubPi_192.168.0.43" -> "192.168.0.43")
                std::string ip = device_id.substr(6);
                sensor_data["ip"] = ip;

                std::cout << "[AI Listener] META from " << device_id
                          << ": " << sensor_data.value("sensor_batch", json::array()).size()
                          << " samples" << std::endl;

                if (on_meta_event_) {
                    on_meta_event_(device_id, sensor_data);
                }
                error_count = 0;
            } catch (const json::parse_error& e) {
                std::cerr << "[AI Listener] META JSON parse error: " << e.what() << std::endl;
                error_count++;
                if (error_count >= 3) break;
            }
        }
    }

    std::cout << "[AI Listener] Stopped for " << device_id << std::endl;

    // 종료된 device_id를 finished 목록에 등록
    {
        std::lock_guard<std::mutex> lock(finished_mutex_);
        finished_ids_.insert(device_id);
    }
}

void SubPiManager::cleanupFinishedThreads() {
    std::set<std::string> ids_to_clean;
    {
        std::lock_guard<std::mutex> lock(finished_mutex_);
        if (finished_ids_.empty()) return;
        ids_to_clean.swap(finished_ids_);
    }

    for (const auto& id : ids_to_clean) {
        auto it = listener_threads_.find(id);
        if (it != listener_threads_.end()) {
            if (it->second.joinable()) {
                it->second.join();
            }
            listener_threads_.erase(it);
        }
    }

    std::cout << "[SubPi] Cleaned up " << ids_to_clean.size()
              << " finished listener(s)." << std::endl;
}
