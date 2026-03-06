#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>

#include "DeviceManager.h"

// ======================== 생성자 / 소멸자 ========================

DeviceManager::DeviceManager() : is_discovering_(false) {
    std::cout << "[DEBUG] DeviceManager Created." << std::endl;

    // SubPiManager 중복 체크 콜백: TCP 연결 전에 확인 (온라인인 장치만 중복 판정)
    subpi_mgr_.setIsDeviceRegistered([this](const std::string& id) -> bool {
        std::lock_guard<std::mutex> lock(device_mutex_);
        auto it = devices_.find(id);
        return it != devices_.end() && it->second.is_online;
    });

    // SubPiManager 콜백 연결: 장치 발견 시 → 신규 등록 또는 오프라인 복구
    subpi_mgr_.setOnDeviceFound([this](const DeviceInfo& info, int tcp_fd) {
        std::lock_guard<std::mutex> lock(device_mutex_);
        if (devices_.count(info.id)) {
            // 이미 등록됨 → 오프라인이면 복구
            DeviceInfo& existing = devices_[info.id];
            if (!existing.is_online) {
                existing.is_online = true;
                existing.command_socket_fd = info.command_socket_fd;
                existing.udp_listen_port = info.udp_listen_port;
                std::cout << "[Monitor] Sub-Pi RECOVERED: " << info.id << std::endl;
                if (on_device_registered_) {
                    on_device_registered_(existing);
                }
            }
            return;
        }
        devices_[info.id] = info;
        std::cout << "Beacon: Registered Sub-Pi at " << info.ip 
                  << " (Port: " << info.udp_listen_port << ")" << std::endl;
        if (on_device_registered_) {
            on_device_registered_(info);
        }
    });

    // SubPiManager AI 이벤트 → 외부 콜백으로 전달
    subpi_mgr_.setOnAiEvent([this](const std::string& device_id, const json& event) {
        if (on_ai_event_) {
            on_ai_event_(device_id, event);
        }
    });

    // SubPiManager IMAGE 이벤트 → 외부 콜백으로 전달
    subpi_mgr_.setOnImageEvent([this](const std::string& device_id, const json& meta, const std::vector<char>& jpeg) {
        if (on_image_event_) {
            on_image_event_(device_id, meta, jpeg);
        }
    });

    // SubPiManager AVAILABLE 이벤트 → 캐시 갱신
    subpi_mgr_.setOnAvailableEvent([this](const std::string& device_id, const json& status) {
        std::lock_guard<std::mutex> lock(device_mutex_);
        device_status_[device_id] = status;
    });

    // OnvifScanner 중복 체크 콜백: curl 전에 확인 (온라인인 장치만 중복 판정)
    onvif_scanner_.setIsDeviceRegistered([this](const std::string& id) -> bool {
        std::lock_guard<std::mutex> lock(device_mutex_);
        auto it = devices_.find(id);
        return it != devices_.end() && it->second.is_online;
    });

    // OnvifScanner 콜백 연결: 카메라 발견 시 → 신규 등록 또는 오프라인 복구
    onvif_scanner_.setOnDeviceFound([this](const DeviceInfo& info) {
        std::lock_guard<std::mutex> lock(device_mutex_);
        if (devices_.count(info.id)) {
            // 이미 등록됨 → 오프라인이면 복구
            DeviceInfo& existing = devices_[info.id];
            if (!existing.is_online) {
                existing.is_online = true;
                std::cout << "[Monitor] Hanwha RECOVERED: " << info.id << std::endl;
                if (on_device_registered_) {
                    on_device_registered_(existing);
                }
            }
            return;
        }
        devices_[info.id] = info;
        std::cout << "ONVIF: Registered Hanwha Camera: " << info.id << std::endl;
        if (on_device_registered_) {
            on_device_registered_(info);
        }
    });
}

DeviceManager::~DeviceManager() {
    stopDiscovery();
}

// ======================== 탐색 시작 / 중지 ========================

void DeviceManager::startDiscovery() {
    if (is_discovering_) return;
    is_discovering_ = true;

    // 하위 매니저 시작 (is_discovering_ 참조 전달)
    subpi_mgr_.start(is_discovering_);
    onvif_scanner_.start(is_discovering_);

    // 모니터링 스레드 시작
    monitor_thread_ = std::thread(&DeviceManager::monitorLoop, this);
}

void DeviceManager::stopDiscovery() {
    if (!is_discovering_) return;
    is_discovering_ = false;

    subpi_mgr_.stop();
    onvif_scanner_.stop();

    if (monitor_thread_.joinable()) monitor_thread_.join();
}

// ======================== 장치 리스트 ========================

std::vector<DeviceInfo> DeviceManager::getDeviceList() {
    std::lock_guard<std::mutex> lock(device_mutex_);
    std::vector<DeviceInfo> list;
    for (const auto& pair : devices_) {
        list.push_back(pair.second);
    }
    return list;
}

json DeviceManager::getDeviceStatusList() {
    std::lock_guard<std::mutex> lock(device_mutex_);
    json result = json::array();
    for (const auto& pair : device_status_) {
        json entry = pair.second;  // {"cpu":..., "memory":..., "temp":..., "uptime":...}
        // devices_ 맵에서 IP 조회하여 추가
        auto it = devices_.find(pair.first);
        if (it != devices_.end()) {
            entry["ip"] = it->second.ip;
        }
        result.push_back(entry);
    }
    return result;
}

// ======================== 모니터링 루프 ========================

void DeviceManager::monitorLoop() {
    std::cout << "[Monitor] Started." << std::endl;

    while (is_discovering_) {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        if (!is_discovering_) break;

        std::vector<std::string> went_offline;
        std::map<std::string, bool> ip_check_cache;  // 한화 IP별 체크 결과 캐시

        {
            std::lock_guard<std::mutex> lock(device_mutex_);
            for (auto& pair : devices_) {
                std::string device_id = pair.first;
                DeviceInfo& info = pair.second;

                if (!info.is_online) continue;  // 이미 오프라인이면 스킵

                // Sub-Pi: TCP 소켓 체크
                if (info.type == DeviceType::SUB_PI) {
                    bool is_alive = true;

                    char buffer[1];
                    int recv_result = recv(info.command_socket_fd, buffer, 1, MSG_PEEK | MSG_DONTWAIT);

                    if (recv_result == 0) {
                        std::cout << "[Monitor] Detected Closed Connection (recv=0): " << device_id << std::endl;
                        is_alive = false;
                    }
                    else if (recv_result < 0) {
                        if (errno != EAGAIN && errno != EWOULDBLOCK) {
                            std::cout << "[Monitor] Recv Error: " << device_id << " (" << strerror(errno) << ")" << std::endl;
                            is_alive = false;
                        }
                    }

                    if (is_alive) {
                        int sent = send(info.command_socket_fd, nullptr, 0, MSG_NOSIGNAL | MSG_DONTWAIT);
                        if (sent < 0) {
                            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                                std::cout << "[Monitor] Send Error (Broken Pipe): " << device_id << std::endl;
                                is_alive = false;
                            }
                        }
                    }

                    if (!is_alive) {
                        went_offline.push_back(device_id);
                    }
                }
                // Hanwha: TCP 554 포트 체크 (같은 IP는 1번만)
                else if (info.type == DeviceType::HANWHA) {
                    if (ip_check_cache.find(info.ip) == ip_check_cache.end()) {
                        ip_check_cache[info.ip] = checkTcpPort(info.ip, 554);
                    }
                    if (!ip_check_cache[info.ip]) {
                        std::cout << "[Monitor] Hanwha offline (TCP 554 failed): " << device_id << std::endl;
                        went_offline.push_back(device_id);
                    }
                }
            }
        }

        // 오프라인 전환 (삭제하지 않음)
        if (!went_offline.empty()) {
            std::lock_guard<std::mutex> lock(device_mutex_);
            for (const auto& id : went_offline) {
                DeviceInfo& info = devices_[id];
                info.is_online = false;

                // Sub-Pi: 소켓 정리
                if (info.type == DeviceType::SUB_PI && info.command_socket_fd != -1) {
                    close(info.command_socket_fd);
                    info.command_socket_fd = -1;
                }

                // RTSP 릴레이 정리 (Sub-Pi만 해당)
                if (on_device_removed_) {
                    on_device_removed_(id);
                }
            }
            std::cout << "[Monitor] " << went_offline.size() << " device(s) went offline." << std::endl;
        }
    }
}

// ======================== TCP 포트 헬스체크 ========================

bool DeviceManager::checkTcpPort(const std::string& ip, int port, int timeout_sec) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;

    // 논블로킹 타임아웃 설정
    struct timeval tv;
    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

    bool result = (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0);
    close(sock);
    return result;
}