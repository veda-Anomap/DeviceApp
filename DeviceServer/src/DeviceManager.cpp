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

    // SubPiManager 콜백 연결: 장치 발견 시 → 여기서 락 + 저장
    subpi_mgr_.setOnDeviceFound([this](const DeviceInfo& info, int tcp_fd) {
        std::lock_guard<std::mutex> lock(device_mutex_);
        if (devices_.count(info.id)) return;  // 중복 방지
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

    // OnvifScanner 콜백 연결: 카메라 발견 시 → 여기서 락 + 저장
    onvif_scanner_.setOnDeviceFound([this](const DeviceInfo& info) {
        std::lock_guard<std::mutex> lock(device_mutex_);
        if (devices_.count(info.id)) return;  // 중복 방지
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

// ======================== 모니터링 루프 ========================

void DeviceManager::monitorLoop() {
    std::cout << "[Monitor] Started." << std::endl;

    while (is_discovering_) {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        if (!is_discovering_) break;

        std::vector<std::string> disconnected_devices;

        {
            std::lock_guard<std::mutex> lock(device_mutex_);
            for (auto& pair : devices_) {
                std::string device_id = pair.first;
                DeviceInfo& info = pair.second;

                // Sub-Pi만 TCP 소켓 체크
                if (info.type == DeviceType::SUB_PI) {
                    bool is_alive = true;

                    // [체크 1] recv(MSG_PEEK) - 상대방이 연결을 끊었는지 확인
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

                    // [체크 2] send 테스트 - 갑작스러운 전원 차단 감지
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
                        disconnected_devices.push_back(device_id);
                    }
                }
            }
        }

        // 죽은 장치 정리
        if (!disconnected_devices.empty()) {
            std::lock_guard<std::mutex> lock(device_mutex_);
            for (const auto& id : disconnected_devices) {
                if (devices_[id].command_socket_fd != -1) {
                    close(devices_[id].command_socket_fd);
                }
                if (on_device_removed_) {
                    on_device_removed_(id);
                }
                devices_.erase(id);
            }
            std::cout << "[DeviceManager] Cleanup Complete. Remaining devices: " << devices_.size() << std::endl;
        }
    }
}