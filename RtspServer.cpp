#include "RtspServer.h"

void RtspServer::start(const std::string& source_url, const std::string& mount_path) {
    source_url_ = source_url;
    mount_path_ = mount_path;

    server_ = gst_rtsp_server_new();
    g_object_set(server_, "service", std::to_string(port_).c_str(), NULL);

    GstRTSPMountPoints *mounts = gst_rtsp_server_get_mount_points(server_);
    GstRTSPMediaFactory *factory = gst_rtsp_media_factory_new();

    std::string pipeline = 
    "( rtspsrc location=" + source_url_ + " latency=0 buffer-mode=0 protocols=udp ! "
    "rtph264depay ! "
    "h264parse config-interval=1 ! " // 여기서 한 번 더 확실히 잡아줍니다.
    "rtph264pay name=pay0 pt=96 config-interval=1 aggregate-mode=zero-latency )";

    gst_rtsp_media_factory_set_launch(factory, pipeline.c_str());
    gst_rtsp_media_factory_set_shared(factory, TRUE);

    gst_rtsp_mount_points_add_factory(mounts, mount_path_.c_str(), factory);
    g_object_unref(mounts);

    // ★ 핵심 추가: 서버를 기본 메인 컨텍스트에 부착 (포트 오픈)
    if (gst_rtsp_server_attach(server_, NULL) == 0) {
        g_print("Failed to attach RTSP server to port %d\n", port_);
        return;
    }

    loop_ = g_main_loop_new(NULL, FALSE);
    server_thread_ = std::thread(&RtspServer::runLoop, this);
    g_print("Relay Server started at rtsp://0.0.0.0:%d%s\n", port_, mount_path.c_str());
}

void RtspServer::runLoop() {
    // 이 함수가 실행되는 순간, 이 스레드는 RTSP 서비스에만 집중합니다.
    g_print("RTSP Server loop is now running...\n");
    g_main_loop_run(loop_); 
    
    // g_main_loop_quit(loop_)가 호출되기 전까지는 이 아래 코드는 실행되지 않습니다.
    g_print("RTSP Server loop has stopped.\n");
}

RtspServer::~RtspServer() {
    if (server_thread_.joinable()) {
        server_thread_.detach(); // 일단 테스트를 위해 분리
    }
}

void RtspServer::stop() {
    if (loop_) {
        g_main_loop_quit(loop_);
    }
}