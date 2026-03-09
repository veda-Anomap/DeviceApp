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

    // 0. 인증 DB 초기화
    if (!auth_manager_.init("users.db")) {
        std::cerr << "[ClientServer] Auth DB init failed. Exiting." << std::endl;
        return;
    }

    // 1. AI 이벤트 수신 → Qt에 즉시 브로드캐스트
    internal_client_.setOnAiEvent([this](const json& event) {
        std::cout << "[ClientServer] AI event → Qt broadcast" << std::endl;
        qt_server_.broadcast(MessageType::AI, event);
    });

    // 1-2. IMAGE 이벤트 수신 → Qt에 즉시 브로드캐스트
    internal_client_.setOnImageEvent([this](const json& meta, const std::vector<char>& jpeg) {
        std::cout << "[ClientServer] IMAGE event → Qt broadcast (" << jpeg.size() << " bytes)" << std::endl;
        qt_server_.broadcastImage(meta, jpeg);
    });

    // 2. DeviceServer에 접속 (카메라 리스트 + AI 이벤트 수신)
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
            qt_server_.broadcastToRole(MessageType::CAMERA, cameras, "user");

            // AVAILABLE: user는 디바이스 상태만, admin은 서버+디바이스
            json device_status = internal_client_.getDeviceStatus();

            json user_avail;
            user_avail["devices"] = device_status;

            json admin_avail;
            admin_avail["server"] = sys_monitor_.getStatus();
            admin_avail["devices"] = device_status;

            qt_server_.broadcastByRole(MessageType::AVAILABLE, user_avail, admin_avail);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        tick++;

        // 1초 대기 (stop() 시 즉시 깨어남)
        std::unique_lock<std::mutex> lock(stop_mutex_);
        stop_cv_.wait_for(lock, std::chrono::milliseconds(900), [this] { return !is_running_.load(); });
    }
}

void ClientController::stop() {
    if (!is_running_) return;
    is_running_ = false;

    // 메인 루프 즉시 깨우기
    stop_cv_.notify_all();

    // InternalClient를 먼저 종료 (qt_server_.stop() 블로킹 중 재연결 방지)
    internal_client_.stop();
    qt_server_.stop();
    std::cout << "[ClientServer] Stopped." << std::endl;
}

void ClientController::onQtMessage(int client_fd, MessageType type, const json& body) {
    switch (type) {
        case MessageType::LOGIN: {
            std::cout << "[ClientServer] LOGIN request." << std::endl;

            std::string username = body.value("id", "");
            std::string password = body.value("pw", "");

            json result = auth_manager_.loginUser(username, password);

            if (result.value("success", false)) {
                // 로그인 성공: role 태깅 + 응답 + 카메라 리스트 전송
                std::string state = result.value("state", "user");
                qt_server_.setClientRole(client_fd, state);

                qt_server_.sendMessage(client_fd, MessageType::SUCCESS, result);

                json cameras = internal_client_.getCameraList();
                qt_server_.sendMessage(client_fd, MessageType::CAMERA, cameras);
            } else {
                qt_server_.sendMessage(client_fd, MessageType::FAIL, result);
            }
            break;
        }

        case MessageType::ASSIGN: {
            std::string action = body.value("action", "");

            if (action == "register") {
                // 회원가입
                std::string username = body.value("id", "");
                std::string email = body.value("email", "");
                std::string password = body.value("pw", "");

                json result = auth_manager_.registerUser(username, email, password);
                MessageType resp = result.value("success", false) ? MessageType::SUCCESS : MessageType::FAIL;
                qt_server_.sendMessage(client_fd, resp, result);

            } else if (action == "list_pending") {
                // 관리자: 승인 대기 목록
                json result = auth_manager_.listPendingUsers();
                qt_server_.sendMessage(client_fd, MessageType::ASSIGN, result);

            } else if (action == "approve") {
                // 관리자: 유저 승인
                std::string target = body.value("target_id", "");
                json result = auth_manager_.approveUser(target);
                MessageType resp = result.value("success", false) ? MessageType::SUCCESS : MessageType::FAIL;
                qt_server_.sendMessage(client_fd, resp, result);

            } else {
                qt_server_.sendMessage(client_fd, MessageType::FAIL, {{"error", "Unknown action"}});
            }
            break;
        }

        case MessageType::DEVICE: {
            std::cout << "[ClientServer] DEVICE command → DeviceServer" << std::endl;
            internal_client_.sendDeviceCommand(body);
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
