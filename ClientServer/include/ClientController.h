#ifndef CLIENT_CONTROLLER_H
#define CLIENT_CONTROLLER_H

#include <atomic>
#include <mutex>
#include <condition_variable>

#include "QtCommServer.h"
#include "InternalClient.h"
#include "SystemMonitor.h"

class ClientController {
public:
    ClientController();
    ~ClientController();

    void run();
    void stop();

private:
    void onQtMessage(int client_fd, MessageType type, const json& body);

    InternalClient internal_client_;
    QtCommServer qt_server_;
    SystemMonitor sys_monitor_;
    std::atomic<bool> is_running_{false};

    // 메인 루프 즉시 깨우기용
    std::condition_variable stop_cv_;
    std::mutex stop_mutex_;
};

#endif
