#ifndef STREAM_CONTROLLER_H
#define STREAM_CONTROLLER_H

#include <atomic>

#include "QtCommServer.h"
#include "RtspServer.h"
#include "InternalClient.h"

class StreamController {
public:
    StreamController();
    ~StreamController();

    void run();   // 블로킹 — Ctrl+C까지 실행
    void stop();

private:
    // Qt 메시지 처리 콜백
    void onQtMessage(int client_fd, MessageType type, const json& body);

    InternalClient internal_client_;
    QtCommServer qt_server_;
    RtspServer rtsp_server_;
    std::atomic<bool> is_running_{false};
};

#endif
