#ifndef _RTSPSERVER_H
#define _RTSPSERVER_H

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>
#include <thread>
#include <vector>
#include <string>
#include <atomic>

#include "Common.h"

class RtspServer {
public:
    RtspServer();
    ~RtspServer();

    void start();
    void stop();
    
    // 장치를 찾았을 때 릴레이 경로를 생성해주는 핵심 함수
    // 예: device_id가 "cam1"이면 "rtsp://서버IP:8554/cam1" 경로 생성
    void addRelayPath(const DeviceInfo& info);
    void removeRelayPath(const std::string& device_id);

private:
    void runMainLoop();

    GMainLoop* loop_;
    GstRTSPServer* server_;
    GstRTSPMountPoints* mounts_;
    std::thread loop_thread_;
    std::atomic<bool> is_running_;
};
#endif