#ifndef COMMON_H
#define COMMON_H

#include <string>
#include <cstdint>

// ======================== 장치 관련 ========================

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

// ======================== Qt 통신 프로토콜 ========================

// 메시지 타입 정의 (1바이트)
enum class MessageType : uint8_t {
    ACK       = 0x00,  // 확인 응답
    LOGIN     = 0x01,  // 로그인 요청
    SUCCESS   = 0x02,  // 성공 응답 (JSON 없음)
    FAIL      = 0x03,  // 실패 응답
    DEVICE    = 0x04,  // 장치 제어 (추후 사용)
    AVAILABLE = 0x05,  // 장치 사용 수치
    AI        = 0x06,  // AI 관련 메타데이터
    CAMERA    = 0x07,  // 카메라 리스트
    ASSIGN    = 0x08,  // 회원 등록
    META      = 0x09,  // 센서 데이터
};

// 패킷 헤더 (총 5바이트: Type 1 + BodyLength 4)
#pragma pack(push, 1)
struct PacketHeader {
    MessageType type;       // 메시지 종류 (1바이트)
    uint32_t body_length;   // JSON 본문 크기 (4바이트, 네트워크 바이트 오더)
};
#pragma pack(pop)

#endif