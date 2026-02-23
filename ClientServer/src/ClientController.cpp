#include <iostream>
#include <thread>

#include "ClientController.h"

ClientController::ClientController() {}
ClientController::~ClientController() { stop(); }

void ClientController::run() {
    std::cout << "========================================" << std::endl;
    std::cout << "   ClientServer - Qt Communication" << std::endl;
    std::cout << "========================================" << std::endl;

    is_running_ = true;

    // 1. DeviceServer에 접속 (카메라 리스트 수신용)
    internal_client_.start("127.0.0.1", 30000);

    // 2. Qt 통신 서버 시작
    qt_server_.start(20000, [this](int client_fd, MessageType type, const json& body) {
        onQtMessage(client_fd, type, body);
    });

    std::cout << "[ClientServer] Running. Press Ctrl+C to stop." << std::endl;

    // 메인 스레드: 주기적으로 카메라 리스트 브로드캐스트
    int tick = 0;
    while (is_running_) {
        if (tick % 5 == 0) {
            json cameras = internal_client_.getCameraList();
            qt_server_.broadcast(MessageType::CAMERA, cameras);
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
        tick++;
    }
}

void ClientController::stop() {
    if (!is_running_) return;
    is_running_ = false;
    qt_server_.stop();
    internal_client_.stop();
    std::cout << "[ClientServer] Stopped." << std::endl;
}

void ClientController::onQtMessage(int client_fd, MessageType type, const json& body) {
    switch (type) {
        case MessageType::LOGIN: {
            std::cout << "[ClientServer] LOGIN request." << std::endl;
            qt_server_.sendMessage(client_fd, MessageType::SUCCESS, {});

            json cameras = internal_client_.getCameraList();
            qt_server_.sendMessage(client_fd, MessageType::CAMERA, cameras);
            break;
        }

        case MessageType::CAMERA: {
            json cameras = internal_client_.getCameraList();
            qt_server_.sendMessage(client_fd, MessageType::CAMERA, cameras);
            break;
        }

        case MessageType::AI: {
            std::cout << "[ClientServer] AI event: " << body.dump() << std::endl;
            qt_server_.broadcast(MessageType::AI, body);
            break;
        }

        default: {
            qt_server_.sendMessage(client_fd, MessageType::ACK, {{"message", "received"}});
            break;
        }
    }
}
