#ifndef DEVICE_CONTROLLER_H
#define DEVICE_CONTROLLER_H

#include <atomic>

#include "DeviceManager.h"
#include "InternalServer.h"
#include "RtspServer.h"

class DeviceController {
public:
    DeviceController();
    ~DeviceController();

    void run();
    void stop();

private:
    json buildCameraListJson();
    std::string getLocalIp();

    DeviceManager device_mgr_;
    RtspServer rtsp_server_;
    InternalServer internal_server_;
    std::atomic<bool> is_running_{false};
};

#endif
