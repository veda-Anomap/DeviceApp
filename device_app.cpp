#include <iostream>
#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

int main(int argc, char *argv[]) {
    GMainLoop *loop;
    GstRTSPServer *server;
    GstRTSPMountPoints *mounts;
    GstRTSPMediaFactory *factory;

    gst_init(&argc, &argv);

    loop = g_main_loop_new(NULL, FALSE);
    server = gst_rtsp_server_new();
    gst_rtsp_server_set_service(server, "8554"); // 서버 파이 B의 포트

    mounts = gst_rtsp_server_get_mount_points(server);
    factory = gst_rtsp_media_factory_new();

    /* * 핵심 파이프라인 설명:
     * 1. rtspsrc: 파이 A의 영상을 가져옴
     * 2. rtph264depay & h264parse: 데이터를 분석 가능한 상태로 정리
     * 3. rtph264pay: 다시 클라이언트(Qt)에게 보낼 패킷으로 포장
     */
    gst_rtsp_media_factory_set_launch(factory,
        "( rtspsrc location=rtsp://192.168.0.58:8554/live latency=100 ! "
        "rtph264depay ! h264parse ! "
        "rtph264pay name=pay0 pt=96 )");

    // 클라이언트는 rtsp://[서버파이B_IP]:8554/relay 로 접속
    gst_rtsp_mount_points_add_factory(mounts, "/relay", factory);
    g_object_unref(mounts);

    gst_rtsp_server_attach(server, NULL);

    g_print("Relay Server started at rtsp://0.0.0.0:8554/relay\n");
    g_main_loop_run(loop);

    return 0;
}