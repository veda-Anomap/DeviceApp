#ifndef AUTH_CONTROLLER_H
#define AUTH_CONTROLLER_H

#include <atomic>

#include "DeviceManager.h"
#include "InternalServer.h"

class AuthController {
public:
    AuthController();
    ~AuthController();

    void run();   // 블로킹 — Ctrl+C까지 실행
    void stop();

private:
    json buildCameraListJson();

    DeviceManager device_mgr_;
    InternalServer internal_server_;
    std::atomic<bool> is_running_{false};
};

#endif
