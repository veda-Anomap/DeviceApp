#include <iostream>
#include <csignal>

#include "DeviceManager.h"
#include "InternalServer.h"

// 전역 포인터 (시그널 핸들러용)
std::atomic<bool> g_running{true};
DeviceManager* g_device_mgr = nullptr;
InternalServer* g_internal = nullptr;

void signalHandler(int signum) {
    std::cout << "\n[AuthServer] Interrupt signal (" << signum << ") received.\n";
    g_running = false;
    if (g_internal) g_internal->stop();
    if (g_device_mgr) g_device_mgr->stopDiscovery();
}

int main() {
    signal(SIGINT, signalHandler);

    std::cout << "========================================" << std::endl;
    std::cout << "   AuthServer - Device Discovery" << std::endl;
    std::cout << "========================================" << std::endl;

    DeviceManager device_mgr;
    InternalServer internal_server;

    g_device_mgr = &device_mgr;
    g_internal = &internal_server;

    // 1. 장치 탐색 시작 (UDP 비콘 + ONVIF 스캔 + 모니터링)
    device_mgr.startDiscovery();

    // 2. 내부 TCP 서버 시작 (StreamServer의 접속을 받음)
    internal_server.start(30000, [&device_mgr]() -> json {
        auto devices = device_mgr.getDeviceList();
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

        return {{"cameras", camera_list}};
    });

    std::cout << "[AuthServer] Running. Press Ctrl+C to stop." << std::endl;

    // 메인 스레드 대기
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}
