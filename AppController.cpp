#include <iostream>
#include <thread>
#include <iomanip>
#include <cstring> // strlen, memset 등을 위해
#include <ctime>   // time, ctime 등을 위해

#include "AppController.h"

AppController::AppController() : is_running_(false) {
    // 컴포넌트 생성
    device_mgr_ = std::make_unique<DeviceManager>();
    relay_server_ = std::make_unique<RtspServer>();
    qt_server_ = std::make_unique<QtCommServer>();
}

AppController::~AppController() {
    stop();
        // 종료 시 필요한 정리 작업
}

void AppController::run() {
    is_running_ = true;
    std::cout << "AppController: Starting system components..." << std::endl;

    // 1. 장치 탐색 스레드 시작 (ONVIF 스캔 및 UDP 비콘 수신)
    device_mgr_->startDiscovery();

    // 2. RTSP 릴레이 서버 시작 (GMainLoop 구동)
    // RtspServer 내부에서 GMainLoop 전용 스레드를 생성합니다.
    relay_server_->start(); 

    // 3. Qt 통신 서버 시작 (포트 20000 대기 스레드 생성)
    qt_server_->start(20000, [this](int client_fd, MessageType type, const json& body) {
        onQtCommandReceived(client_fd, type, body);
    });

    

    std::cout << "AppController: All threads are running." << std::endl;
    
    // 메인 스레드는 시스템 상태를 감시하거나 종료 신호를 대기합니다.
    int tick = 0;
    while (is_running_) {
        // 1초마다 시스템 상태 업데이트 (릴레이 자동 생성 등)
        updateSystemState();

        // 5초마다 연결된 Qt 클라이언트에 카메라 리스트 브로드캐스트
        if (tick % 5 == 0) {
            broadcastCameraList();
        }

        printStatus();

        std::this_thread::sleep_for(std::chrono::seconds(1));
        tick++;
    }
}

void AppController::stop() {
    if (is_running_) {
        is_running_ = false;
        device_mgr_->stopDiscovery();
        relay_server_->stop();
        qt_server_->stop();
        std::cout << "AppController: Stopped." << std::endl;
    }
}

void AppController::updateSystemState() {
    auto devices = device_mgr_->getDeviceList();
    
    for (const auto& dev : devices) {
        // [수정] 서브 카메라(UDP)인 경우에만 릴레이 경로를 생성합니다.
        if (dev.type == DeviceType::SUB_PI) {
            
            // 이미 릴레이 생성했는지 확인
            if (processed_relay_ids_.find(dev.id) == processed_relay_ids_.end()) {
                std::cout << "[System] Creating Relay for Sub-Pi: " << dev.id << std::endl;
                
                // 릴레이 서버에 등록 (UDP -> RTSP 변환)
                relay_server_->addRelayPath(dev);
                
                processed_relay_ids_.insert(dev.id);
            }
        }
        // [중요] 한화 카메라는 여기서 아무것도 안 합니다! 
        // 그냥 DeviceManager 리스트에 잘 보관되어 있다가, 나중에 Qt가 "리스트 줘" 하면 그때 URL만 건네주면 됩니다.
    }
}

// 기존 checkCLI의 이름을 printStatus로 변경 (한 번만 출력하는 함수로 변경)
void AppController::printStatus() {
    auto devices = device_mgr_->getDeviceList();

    // 화면 갱신 (리눅스 터미널 클리어, 윈도우는 cls)
    // system("clear"); // 디버깅 로그가 지워질 수 있으니 개발 중에는 주석 처리 추천

    time_t now = time(0);
    char* dt = ctime(&now);
    if(dt) dt[strlen(dt)-1] = '\0'; // 개행 제거 (안전성 추가)
    
    std::cout << "\n[" << dt << "] System Status | Devices Found: " << devices.size() << std::endl;
    std::cout << "----------------------------------------------------------------------" << std::endl;
    std::cout << std::left << std::setw(28) << "ID" 
              << std::setw(16) << "IP" 
              << std::setw(10) << "TYPE" 
              << std::setw(10) << "STATUS" 
              << "INFO (Port/URL)" << std::endl;
    std::cout << "----------------------------------------------------------------------" << std::endl;

    if (devices.empty()) {
        std::cout << "   (No devices found yet... Waiting for beacons/ONVIF)" << std::endl;
    } else {
        for (const auto& dev : devices) {
            // [수정] Enum을 문자열로 변환하여 출력
            std::string typeStr = (dev.type == DeviceType::SUB_PI) ? "SUB_PI" : "HANWHA";
            
            // 정보 필드 (타입에 따라 다르게 출력)
            std::string infoStr;
            if (dev.type == DeviceType::SUB_PI) {
                infoStr = "UDP Port: " + std::to_string(dev.udp_listen_port);
            } else {
                infoStr = "RTSP Src"; // URL은 너무 길어서 생략하거나 필요시 출력
            }

            std::cout << std::left << std::setw(28) << dev.id 
                      << std::setw(16) << dev.ip 
                      << std::setw(10) << typeStr 
                      << std::setw(10) << (dev.is_online ? "ONLINE" : "OFFLINE") 
                      << infoStr << std::endl;
        }
    }
    std::cout << "----------------------------------------------------------------------" << std::endl;
}

// ======================== Qt 메시지 처리 콜백 ========================

void AppController::onQtCommandReceived(int client_fd, MessageType type, const json& body) {
    switch (type) {
        case MessageType::LOGIN: {
            // 프로토타입: 인증 없이 바로 SUCCESS 응답 (JSON 없음)
            std::cout << "[AppController] LOGIN request received." << std::endl;
            qt_server_->sendMessage(client_fd, MessageType::SUCCESS, {});

            // SUCCESS 후 바로 카메라 리스트 전송
            qt_server_->sendMessage(client_fd, MessageType::CAMERA, buildCameraListJson());
            std::cout << "[AppController] Sent SUCCESS + CAMERA list to fd: " << client_fd << std::endl;
            break;
        }

        case MessageType::CAMERA: {
            // 클라이언트가 카메라 리스트 요청 시 응답
            qt_server_->sendMessage(client_fd, MessageType::CAMERA, buildCameraListJson());
            std::cout << "[AppController] CAMERA list sent on request." << std::endl;
            break;
        }

        case MessageType::AI: {
            std::cout << "[AppController] AI event received: " << body.dump() << std::endl;
            qt_server_->broadcast(MessageType::AI, body);
            break;
        }

        default: {
            qt_server_->sendMessage(client_fd, MessageType::ACK, {{"message", "received"}});
            break;
        }
    }
}

// ======================== 카메라 리스트 ========================

json AppController::buildCameraListJson() {
    auto devices = device_mgr_->getDeviceList();
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
}

void AppController::broadcastCameraList() {
    json camera_json = buildCameraListJson();
    qt_server_->broadcast(MessageType::CAMERA, camera_json);
}