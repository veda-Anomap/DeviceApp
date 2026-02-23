#ifndef APP_CONTROLLER_H
#define APP_CONTROLLER_H

#include "DeviceManager.h"
#include "RtspServer.h"
#include "QtCommServer.h"
#include <memory>
#include <set>    // 중복 등록 방지용
#include <string>

class AppController {
public:
    AppController();
    ~AppController();

    // 시스템 초기화 및 실행 (Makefile에서 지정한 device_app.cpp의 진입점)
    void run();
    void stop();

private:
    // 하위 컴포넌트들
    std::unique_ptr<DeviceManager> device_mgr_;
    std::unique_ptr<RtspServer> relay_server_;
    std::unique_ptr<QtCommServer> qt_server_;

    bool is_running_;

    // [추가] 이미 RTSP 릴레이 파이프라인을 생성한 장치 ID 보관
    // 이를 통해 매 루프마다 새로운 파이프라인이 중복 생성되는 것을 막습니다.
    std::set<std::string> processed_relay_ids_;

    // [추가] 장치 리스트를 체크하고 릴레이를 연결하는 내부 로직
    void updateSystemState();

    // Qt 클라이언트 메시지 수신 콜백
    void onQtCommandReceived(int client_fd, MessageType type, const json& body);

    // 카메라 리스트 JSON 생성 및 브로드캐스트
    json buildCameraListJson();
    void broadcastCameraList();

    void printStatus();
};

#endif