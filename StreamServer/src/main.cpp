#include <iostream>
#include <csignal>
#include <thread>

#include "QtCommServer.h"
#include "RtspServer.h"
#include "InternalClient.h"

// 전역 포인터 (시그널 핸들러용)
std::atomic<bool> g_running{true};
QtCommServer* g_qt_server = nullptr;
RtspServer* g_rtsp_server = nullptr;
InternalClient* g_internal = nullptr;

void signalHandler(int signum) {
    std::cout << "\n[StreamServer] Interrupt signal (" << signum << ") received.\n";
    g_running = false;
    if (g_qt_server) g_qt_server->stop();
    if (g_rtsp_server) g_rtsp_server->stop();
    if (g_internal) g_internal->stop();
}

int main() {
    signal(SIGINT, signalHandler);

    std::cout << "========================================" << std::endl;
    std::cout << "   StreamServer - Qt Comm + RTSP" << std::endl;
    std::cout << "========================================" << std::endl;

    InternalClient internal_client;
    QtCommServer qt_server;
    RtspServer rtsp_server;

    g_qt_server = &qt_server;
    g_rtsp_server = &rtsp_server;
    g_internal = &internal_client;

    // 1. AuthServer에 접속 (카메라 리스트 수신용)
    internal_client.start("127.0.0.1", 30000);

    // 2. RTSP 릴레이 서버 시작
    rtsp_server.start();

    // 3. Qt 통신 서버 시작
    qt_server.start(20000, [&internal_client, &qt_server](int client_fd, MessageType type, const json& body) {
        switch (type) {
            case MessageType::LOGIN: {
                std::cout << "[StreamServer] LOGIN request." << std::endl;
                qt_server.sendMessage(client_fd, MessageType::SUCCESS, {});

                // SUCCESS 후 바로 카메라 리스트 전송
                json cameras = internal_client.getCameraList();
                qt_server.sendMessage(client_fd, MessageType::CAMERA, cameras);
                break;
            }

            case MessageType::CAMERA: {
                json cameras = internal_client.getCameraList();
                qt_server.sendMessage(client_fd, MessageType::CAMERA, cameras);
                break;
            }

            case MessageType::AI: {
                std::cout << "[StreamServer] AI event: " << body.dump() << std::endl;
                qt_server.broadcast(MessageType::AI, body);
                break;
            }

            default: {
                qt_server.sendMessage(client_fd, MessageType::ACK, {{"message", "received"}});
                break;
            }
        }
    });

    std::cout << "[StreamServer] Running. Press Ctrl+C to stop." << std::endl;

    // 메인 스레드: 주기적으로 카메라 리스트 브로드캐스트
    int tick = 0;
    while (g_running) {
        if (tick % 5 == 0) {
            json cameras = internal_client.getCameraList();
            qt_server.broadcast(MessageType::CAMERA, cameras);
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
        tick++;
    }

    return 0;
}
