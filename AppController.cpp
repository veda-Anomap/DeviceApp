#include <iostream>
#include <thread>
#include <iomanip>

#include "AppController.h"

using namespace std;

AppController::AppController() : is_running_(false) {
    // 컴포넌트 생성
    device_mgr_ = std::make_unique<DeviceManager>();
    //relay_server_ = std::make_unique<RtspServer>();
    //qt_server_ = std::make_unique<QtCommServer>();
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

    std::cout << "========================================" << std::endl;
    std::cout << "   SYSTEM STARTED: WAITING FOR DEVICES  " << std::endl;
    std::cout << "========================================" << std::endl;

    // 2. 메인 루프 (1초마다 모니터링)
    while (is_running_) {
        // 장치 리스트 가져오기 (Thread-Safe)
        auto devices = device_mgr_->getDeviceList();

        // 화면 갱신 (리눅스 터미널 클리어, 윈도우는 cls)
        // system("clear"); 
        
        // 현재 시간 출력
        time_t now = time(0);
        char* dt = ctime(&now);
        dt[strlen(dt)-1] = '\0'; // 개행 제거
        
        std::cout << "\n[" << dt << "] Connected Devices: " << devices.size() << std::endl;
        std::cout << "------------------------------------------------------------" << std::endl;
        std::cout << std::left << std::setw(20) << "ID" 
                  << std::setw(15) << "IP" 
                  << std::setw(10) << "TYPE" 
                  << "STATUS" << std::endl;
        std::cout << "------------------------------------------------------------" << std::endl;

        if (devices.empty()) {
            std::cout << "   (No devices found yet...)" << std::endl;
        } else {
            for (const auto& dev : devices) {
                std::cout << std::left << std::setw(20) << dev.id 
                          << std::setw(15) << dev.ip 
                          << std::setw(10) << dev.type 
                          << (dev.is_online ? "ONLINE" : "OFFLINE") << std::endl;
            }
        }
        std::cout << "------------------------------------------------------------" << std::endl;

        // 1초 대기
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // 2. Qt 통신 서버 시작 (포트 20000 대기 스레드 생성)
    // qt_server 내부에서 std::thread를 생성하도록 설계합니다.
    //qt_server_->start(20000); 

    // 3. RTSP 릴레이 서버 시작 (GMainLoop 구동)
    // RtspServer 내부에서 GMainLoop 전용 스레드를 생성합니다.
    //relay_server_->start(); 

    std::cout << "AppController: All threads are running." << std::endl;

    // 메인 스레드는 시스템 상태를 감시하거나 종료 신호를 대기합니다.
    while (is_running_) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void AppController::stop() {
    if (is_running_) {
        is_running_ = false;
        device_mgr_->stopDiscovery();
        std::cout << "AppController: Stopped." << std::endl;
    }    
    // 안전한 종료 순서
    //relay_server_->stop();
    //qt_server_->stop();
}