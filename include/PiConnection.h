#ifndef _PICONNECTION_H
#define _PICONNECTION_H

#include <iostream>
#include <thread>
#include <vector>
#include <functional>
#include <netinet/in.h>

class PiConnection {
private:
    int port_;
    int server_fd_;
    bool is_running_;
    std::thread listen_thread_;
public:
    PiConnection();
    ~PiConnection();
    void start(int port);
    void runServer();
};
#endif