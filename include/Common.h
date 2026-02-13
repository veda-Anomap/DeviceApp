#ifndef COMMON_H
#define COMMON_H

#include <string>

enum class DeviceType {
    HANWHA, // RTSP Pull 방식
    SUB_PI  // UDP Push 방식 (서버가 포트 열고 대기)
};

struct DeviceInfo {
    std::string id;
    std::string ip;
    DeviceType type;
    bool is_online;

    // 타입별 데이터
    // HANWHA용 (rtsp://...)
    std::string source_url; 

    // SUB_PI용 (15001...)
    int udp_listen_port;    
    int command_socket_fd;
    
};

#endif