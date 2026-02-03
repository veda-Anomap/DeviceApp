#include <iostream>

#include "AppController.h"

using namespace std;

AppController::AppController() {
        // GStreamer 초기화는 여기서 딱 한 번!
        gst_init(nullptr, nullptr); 
        cout << "GStreamer Initialized\n";
}

AppController::~AppController() {
        // 종료 시 필요한 정리 작업
}

void AppController::run(int argc, char* argv[]) {
        // 1. RTSP 릴레이 서버 시작
        rtsp_server_.start("rtsp://192.168.0.58:8554/live", "/relay");

        // 2. (추가 예정) 제어 소켓 서버 시작
        //pi_connection.start(6000);

        cout << "All systems GO. Press Enter to terminate.\n";
        cin.get();
}