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
    
    // 타입별 데이터
    std::string source_url; // HANWHA용 (rtsp://...)
    int udp_listen_port;    // SUB_PI용 (15001...)
    
    bool is_online;
};

#endif