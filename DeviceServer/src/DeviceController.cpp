#include <iostream>
#include <thread>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "DeviceController.h"

DeviceController::DeviceController() {}
DeviceController::~DeviceController() { stop(); }

void DeviceController::run() {
    std::cout << "========================================" << std::endl;
    std::cout << "   DeviceServer - Discovery + RTSP" << std::endl;
    std::cout << "========================================" << std::endl;

    is_running_ = true;

    // 1. 장치 발견 → RTSP 릴레이 자동 생성 연동
    device_mgr_.setOnDeviceRegistered([this](const DeviceInfo& info) {
        rtsp_server_.addRelayPath(info);
    });

    // 1-2. 장치 연결 끊김 → RTSP 릴레이 제거
    device_mgr_.setOnDeviceRemoved([this](const std::string& device_id) {
        rtsp_server_.removeRelayPath(device_id);
    });

    // 1-3. AI 이벤트 → ClientServer로 전달
    device_mgr_.setOnAiEvent([this](const std::string& device_id, const json& event) {
        internal_server_.broadcastAiEvent(event);
    });

    // 2. 장치 탐색 시작 (UDP 비콘 + ONVIF 스캔 + 모니터링)
    device_mgr_.startDiscovery();

    // 2. RTSP 릴레이 서버 시작
    rtsp_server_.start();

    // 3. 내부 TCP 서버 시작 (ClientServer의 접속을 받음)
    internal_server_.start(30000, [this]() -> json {
        return buildCameraListJson();
    });

    std::cout << "[DeviceServer] Running. Press Ctrl+C to stop." << std::endl;

    // 메인 스레드 대기
    while (is_running_) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void DeviceController::stop() {
    if (!is_running_) return;
    is_running_ = false;
    internal_server_.stop();
    rtsp_server_.stop();
    device_mgr_.stopDiscovery();
    std::cout << "[DeviceServer] Stopped." << std::endl;
}

json DeviceController::buildCameraListJson() {
    auto devices = device_mgr_.getDeviceList();
    std::string local_ip = getLocalIp();
    json camera_list = json::array();

    for (const auto& dev : devices) {
        json d;
        d["is_online"] = dev.is_online;
        d["ip"] = dev.ip;

        if (dev.type == DeviceType::SUB_PI) {
            d["source_url"] = "rtsp://" + local_ip + ":8554/" + dev.id;
        } else {
            d["source_url"] = dev.source_url;
        }

        d["type"] = (dev.type == DeviceType::SUB_PI) ? "SUB_PI" : "HANWHA";
        camera_list.push_back(d);
    }

    return camera_list;
}

std::string DeviceController::getLocalIp() {
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == -1) return "127.0.0.1";

    std::string result = "127.0.0.1";
    for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &((struct sockaddr_in*)ifa->ifa_addr)->sin_addr, ip, sizeof(ip));

        std::string ip_str(ip);
        // 루프백(127.0.0.1) 제외, 실제 LAN IP 선택
        if (ip_str != "127.0.0.1") {
            result = ip_str;
            break;
        }
    }
    freeifaddrs(ifaddr);
    return result;
}
