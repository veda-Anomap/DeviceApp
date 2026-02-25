#include <iostream>

#include "RtspServer.h"

void RtspServer::start() {
    if (is_running_) return;
    is_running_ = true;

    if (!server_) {
        server_ = gst_rtsp_server_new();
        g_object_set(server_, "service", "8554", NULL);
        
        // 이 mounts_ 포인터가 없으면 addRelayPath 할 때 무조건 펑 터집니다.
        mounts_ = gst_rtsp_server_get_mount_points(server_);
    }
    
    // GStreamer의 엔진을 돌리기 전에 이미 Mounts(장부)는 준비되어 있어야 함
    if (gst_rtsp_server_attach(server_, NULL) == 0) {
        std::cerr << "RtspServer: Failed to attach to default context!" << std::endl;
    }

    // 2. GMainLoop 구동을 위한 전용 스레드 생성
    loop_thread_ = std::thread(&RtspServer::runMainLoop, this);
    std::cout << "RtspServer: GMainLoop thread started." << std::endl;
}

void RtspServer::stop() {
    if (!is_running_) return;
    is_running_ = false;

    if (loop_) {
        g_main_loop_quit(loop_); // 루프 종료 신호
    }

    if (loop_thread_.joinable()) {
        loop_thread_.join();
    }

    if (loop_) {
        g_main_loop_unref(loop_);
        loop_ = nullptr;
    }
}

void RtspServer::runMainLoop() {
    loop_ = g_main_loop_new(nullptr, FALSE);
    
    std::cout << "RtspServer: GMainLoop is running." << std::endl;
    g_main_loop_run(loop_); // 여기서 스레드가 블로킹됨
    
    std::cout << "RtspServer: GMainLoop has stopped." << std::endl;
}

RtspServer::RtspServer() : loop_(nullptr), server_(nullptr), mounts_(nullptr), is_running_(false) {
    gst_init(nullptr, nullptr); // GStreamer 필수 초기화
}

RtspServer::~RtspServer() {
    stop();
}

void RtspServer::addRelayPath(const DeviceInfo& info) {
    if (!mounts_) return; // (아까 세그폴트 방지용)

    // 한화 카메라는 릴레이 안 함! 서브 파이만 처리
    if (info.type != DeviceType::SUB_PI) {
        return; 
    }

    // 오직 서브 파이용 파이프라인(udpsrc)만 남깁니다.
    std::string pipeline_str = 
        "( udpsrc port=" + std::to_string(info.udp_listen_port) + 
        " caps=\"application/x-rtp, payload=96\" ! "
        "rtph264depay ! h264parse ! rtph264pay name=pay0 pt=96 )";

    GstRTSPMediaFactory* factory = gst_rtsp_media_factory_new();
    gst_rtsp_media_factory_set_launch(factory, pipeline_str.c_str());
    gst_rtsp_media_factory_set_shared(factory, TRUE);

    std::string path = "/" + info.id;
    gst_rtsp_mount_points_add_factory(mounts_, path.c_str(), factory);

    std::cout << "[RELAY] Active: UDP Port " << info.udp_listen_port << " -> " << path << std::endl;
}

void RtspServer::removeRelayPath(const std::string& device_id) {
    if (!mounts_) return;

    std::string path = "/" + device_id;
    gst_rtsp_mount_points_remove_factory(mounts_, path.c_str());
    std::cout << "[RELAY] Removed: " << path << std::endl;
}