#ifndef _RTSPSERVER_H
#define _RTSPSERVER_H

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>
#include <string>
#include <thread>

class RtspServer {
private:	
    static void media_configure(GstRTSPMediaFactory * factory, GstRTSPThread * media, gpointer user_data);
    void runLoop();

    int port_;
    std::string source_url_;
    std::string mount_path_;
    
    GMainLoop *loop_;
    GstRTSPServer *server_;
    std::thread server_thread_;
public:
    RtspServer(int port = 8554): port_(port){}
    ~RtspServer();

    // 릴레이 시작 (파이 B의 주소를 인자로 받음)
    void start(const std::string& source_url, const std::string& mount_path);
    
    // 릴레이 정지
    void stop();

    // [확장 예정] 클라이언트 접속 제어 (수락/거절)
    void setAccessControl(bool allow);
	
};
#endif