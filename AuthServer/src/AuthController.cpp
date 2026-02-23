#include <iostream>
#include <thread>

#include "AuthController.h"

AuthController::AuthController() {}
AuthController::~AuthController() { stop(); }

void AuthController::run() {
    std::cout << "========================================" << std::endl;
    std::cout << "   AuthServer - Device Discovery" << std::endl;
    std::cout << "========================================" << std::endl;

    is_running_ = true;

    // 1. 장치 탐색 시작 (UDP 비콘 + ONVIF 스캔 + 모니터링)
    device_mgr_.startDiscovery();

    // 2. 내부 TCP 서버 시작 (StreamServer의 접속을 받음)
    internal_server_.start(30000, [this]() -> json {
        return buildCameraListJson();
    });

    std::cout << "[AuthServer] Running. Press Ctrl+C to stop." << std::endl;

    // 메인 스레드 대기
    while (is_running_) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void AuthController::stop() {
    if (!is_running_) return;
    is_running_ = false;
    internal_server_.stop();
    device_mgr_.stopDiscovery();
    std::cout << "[AuthServer] Stopped." << std::endl;
}

json AuthController::buildCameraListJson() {
    auto devices = device_mgr_.getDeviceList();
    json camera_list = json::array();

    for (const auto& dev : devices) {
        json d;
        d["id"] = dev.id;
        d["ip"] = dev.ip;
        d["type"] = (dev.type == DeviceType::SUB_PI) ? "SUB_PI" : "HANWHA";
        d["is_online"] = dev.is_online;

        if (dev.type == DeviceType::SUB_PI) {
            d["udp_port"] = dev.udp_listen_port;
        } else {
            d["source_url"] = dev.source_url;
        }
        camera_list.push_back(d);
    }

    return camera_list;
}
