#ifndef DEVICE_MANAGER_H
#define DEVICE_MANAGER_H

#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <string>
#include <map>

// 장치 정보를 담는 구조체
struct DeviceInfo {
    std::string id;          // 고유 ID
    std::string ip;          // IP 주소
    std::string rtsp_url;    // RTSP 스트리밍 주소
    std::string type;        // "HANWHA" 또는 "SUB_PI"
    bool is_online;          // 현재 연결 상태
};

class DeviceManager {
public:
    DeviceManager();
    ~DeviceManager();

    // 장치 탐색 시작 (ONVIF 및 UDP Beacon 스레드 실행)
    void startDiscovery();
    void stopDiscovery();

    std::vector<DeviceInfo> getDeviceList();
private:
    // 1. 스레드 관리를 위한 벡터
    std::vector<std::thread> discovery_threads_;

    // 2. 스레드 루프 제어를 위한 원자적(Atomic) 플래그
    // 여러 스레드가 동시에 참조해도 안전하도록 std::atomic을 사용합니다.
    std::atomic<bool> is_discovering_;

    // 3. 장치 리스트 및 동기화 뮤텍스
    std::map<std::string, DeviceInfo> devices_;
    std::mutex device_mutex_;

    // 스레드에서 실행될 함수들
    void runBeaconReceiver(); // UDP 10001 포트 감시
    void runOnvifScanner();    // ONVIF 스캔
};

#endif