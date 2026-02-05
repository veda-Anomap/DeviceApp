#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>

#include "DeviceManager.h"

const std::string ONVIF_PROBE_MSG = 
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<e:Envelope xmlns:e=\"http://www.w3.org/2003/05/soap-envelope\" "
    "xmlns:w=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\" "
    "xmlns:d=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\" "
    "xmlns:dn=\"http://www.onvif.org/ver10/network/wsdl\">"
    "<e:Header>"
    "<w:MessageID>uuid:84ede3de-7dec-11d0-c360-F01234567890</w:MessageID>"
    "<w:To>urn:schemas-xmlsoap-org:ws:2005:04:discovery</w:To>"
    "<w:Action>http://schemas.xmlsoap.org/ws/2005/04/discovery/Probe</w:Action>"
    "</e:Header>"
    "<e:Body>"
    "<d:Probe>"
    "<d:Types>dn:NetworkVideoTransmitter</d:Types>" // 카메라도 찾는 필터
    "</d:Probe>"
    "</e:Body>"
    "</e:Envelope>";

void DeviceManager::startDiscovery() {
    if (is_discovering_) return; // 이미 실행 중이면 무시
    is_discovering_ = true;
    std::cout << "DeviceManager: Starting discovery threads..." << std::endl;
    
    // 10001번 포트 UDP 비콘 수신 스레드
    discovery_threads_.emplace_back(&DeviceManager::runBeaconReceiver, this);
    
    // ONVIF 장치 스캔 스레드 (주기적 실행)
    discovery_threads_.emplace_back(&DeviceManager::runOnvifScanner, this);
}

void DeviceManager::stopDiscovery() {
    is_discovering_ = false; // 스레드 루프를 멈추게 하는 플래그

    for (auto& t : discovery_threads_) {
        if (t.joinable()) {
            t.join(); // 스레드가 완전히 끝날 때까지 대기
        }
    }
    discovery_threads_.clear();
}

#include "DeviceManager.h"


void DeviceManager::runBeaconReceiver() {
    std::cout << "[DEBUG] BeaconReceiver Thread: STARTING..." << std::endl; // 시작 확인
    int sockfd;
    struct sockaddr_in servaddr, cliaddr;

    // 1. UDP 소켓 생성
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("[ERROR] Beacon: Socket creation failed"); // 에러 원인 출력
        return;
    }
    std::cout << "[DEBUG] BeaconReceiver: Socket Created (fd: " << sockfd << ")" << std::endl;

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY; // 모든 인터페이스로부터 수신
    servaddr.sin_port = htons(10001);      // 약속된 10001번 포트

    // 2. 포트 바인딩
    int bind_result = bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr));
    if (bind_result < 0) {
        perror("[ERROR] Beacon: Bind failed"); // "Address already in use" 인지 "Permission denied" 인지 나옴
        close(sockfd);
        return;
    }
    std::cout << "[DEBUG] BeaconReceiver: Bind SUCCESS! Listening on 10001..." << std::endl;

    char buffer[1024];
    while (is_discovering_) { // atomic 플래그가 true인 동안 루프
        struct timeval tv;
        tv.tv_sec = 1;  // 1초마다 루프 체크를 위해 타임아웃 설정
        tv.tv_usec = 0;
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        socklen_t len = sizeof(cliaddr);
        int n = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0, (struct sockaddr *)&cliaddr, &len);

        if (n > 0) {
            buffer[n] = '\0';
            std::string sender_ip = inet_ntoa(cliaddr.sin_addr);
            std::string message(buffer);

            // [핵심] 장치 등록 로직 호출
            // 예: 비콘 메시지에 "SUB_CAMERA_READY" 등이 포함되어 있는지 확인
            if (message.find("SUB_PI_ALIVE") != std::string::npos) {
                DeviceInfo info;
                info.id = "SubPi_" + sender_ip; // IP 기반 ID 생성
                info.ip = sender_ip;
                info.type = "SUB_PI";
                info.rtsp_url = "rtsp://" + sender_ip + ":8554/relay"; // 서브파이의 기본 RTSP 주소
                info.is_online = true;

                // 뮤텍스로 보호하며 맵에 저장
                {
                    std::lock_guard<std::mutex> lock(device_mutex_);
                    devices_[info.id] = info;
                }
                std::cout << "Beacon: New Sub-Pi discovered at " << sender_ip << std::endl;
            }
        }
    }

    close(sockfd);
    std::cout << "Beacon: Thread stopped." << std::endl;
}

void DeviceManager::runOnvifScanner() {
    int sockfd;
    struct sockaddr_in multi_addr;
    
    // 1. UDP 소켓 생성
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        std::cerr << "ONVIF: Socket creation failed" << std::endl;
        return;
    }

    // 2. 수신 타임아웃 설정 (3초)
    // 응답을 무한정 기다리지 않고, 3초간 수집 후 휴식하기 위함
    struct timeval tv;
    tv.tv_sec = 3;
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // 3. 멀티캐스트 목적지 설정 (239.255.255.250:3702)
    memset(&multi_addr, 0, sizeof(multi_addr));
    multi_addr.sin_family = AF_INET;
    multi_addr.sin_addr.s_addr = inet_addr("239.255.255.250");
    multi_addr.sin_port = htons(3702);

    std::cout << "ONVIF: Scanner thread started." << std::endl;

    while (is_discovering_) {
        // 4. Probe 메시지 전송 (Throwing the bottle)
        sendto(sockfd, ONVIF_PROBE_MSG.c_str(), ONVIF_PROBE_MSG.size(), 0,
               (struct sockaddr *)&multi_addr, sizeof(multi_addr));
        
        // 5. 응답 수집 루프 (3초간 유지)
        // 한 번 던지고 여러 대의 카메라가 응답하는 것을 다 받아야 함
        char buffer[4096];
        struct sockaddr_in cam_addr;
        socklen_t addr_len = sizeof(cam_addr);

        while (true) {
            int n = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0,
                             (struct sockaddr *)&cam_addr, &addr_len);
            
            if (n < 0) break; // 타임아웃 발생 시 루프 탈출 -> 잠시 휴식

            buffer[n] = '\0';
            std::string response(buffer);
            std::string cam_ip = inet_ntoa(cam_addr.sin_addr);

            // [검증] 응답이 ProbeMatch인지 확인
            if (response.find("ProbeMatch") != std::string::npos) {
                
                // 중복 확인 및 등록
                {
                    std::lock_guard<std::mutex> lock(device_mutex_);
                    
                    // ID를 IP기반으로 단순화 (실제로는 UUID 파싱 권장)
                    std::string id = "Hanwha_" + cam_ip;

                    // 아직 맵에 없는 새로운 카메라라면?
                    if (devices_.find(id) == devices_.end()) {
                        DeviceInfo info;
                        info.id = id;
                        info.ip = cam_ip;
                        info.type = "HANWHA";
                        info.is_online = true;
                        
                        // 한화 카메라 RTSP 주소 규칙 (프로파일2: 저해상도/NVR용)
                        info.rtsp_url = "rtsp://admin:password@" + cam_ip + "/profile2/media.smp";

                        devices_[id] = info;
                        std::cout << "ONVIF: Found Camera at " << cam_ip << std::endl;
                    }
                }
            }
        }

        // 6. 다음 스캔까지 휴식 (예: 30초)
        // 너무 자주 쏘면 네트워크 부하가 생김
        for(int i=0; i<30; ++i) {
            if(!is_discovering_) break;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    close(sockfd);
    std::cout << "ONVIF: Scanner thread stopped." << std::endl;
}

std::vector<DeviceInfo> DeviceManager::getDeviceList() {
    std::lock_guard<std::mutex> lock(device_mutex_); // [중요] 읽는 동안 쓰기 금지!
    
    std::vector<DeviceInfo> list;
    for (const auto& pair : devices_) {
        list.push_back(pair.second);
    }
    return list; // 안전하게 복사된 리스트 반환
}

DeviceManager::DeviceManager() : is_discovering_(false) {
    std::cout << "[DEBUG] DeviceManager Created." << std::endl;
}

DeviceManager::~DeviceManager() {
    stopDiscovery();
}