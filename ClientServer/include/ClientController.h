#ifndef CLIENT_CONTROLLER_H
#define CLIENT_CONTROLLER_H

#include <atomic>

#include "QtCommServer.h"
#include "InternalClient.h"

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
    std::atomic<bool> is_running_{false};
};

#endif
