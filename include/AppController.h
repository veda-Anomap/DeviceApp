#ifndef APP_CONTROLLER_H
#define APP_CONTROLLER_H

#include "DeviceManager.h"
#include "RtspServer.h"
#include "QtCommServer.h"
#include <memory>

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

    // 이벤트 처리용 콜백 함수들
    void onNewDeviceFound(const DeviceInfo& info);
    void onQtCommandReceived(const std::string& targetId, const std::string& cmd);
};

#endif