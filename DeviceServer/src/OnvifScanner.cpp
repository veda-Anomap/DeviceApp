#include <iostream>
#include <fstream>
#include <sstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>

#include "OnvifScanner.h"

const std::string ONVIF_PROBE_MSG = 
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<e:Envelope xmlns:e=\"http://www.w3.org/2003/05/soap-envelope\" "
    "xmlns:w=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\" "
    "xmlns:d=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\" "
    "xmlns:dn=\"http://www.onvif.org/ver10/network/wsdl\">"
    "<e:Header>"
    "<w:MessageID>uuid:84ede3de-7dec-11d0-c360-f01234567890</w:MessageID>"
    "<w:To e:mustUnderstand=\"true\">urn:schemas-xmlsoap-org:ws:2005:04:discovery</w:To>"
    "<w:Action e:mustUnderstand=\"true\">http://schemas.xmlsoap.org/ws/2005/04/discovery/Probe</w:Action>"
    "</e:Header>"
    "<e:Body>"
    "<d:Probe>"
    "<d:Types>dn:NetworkVideoTransmitter</d:Types>"
    "</d:Probe>"
    "</e:Body>"
    "</e:Envelope>";

OnvifScanner::OnvifScanner() {}

OnvifScanner::~OnvifScanner() {
    stop();
}

void OnvifScanner::start(std::atomic<bool>& is_running) {
    is_running_ = &is_running;
    scan_thread_ = std::thread(&OnvifScanner::runScan, this);
}

void OnvifScanner::stop() {
    if (scan_thread_.joinable()) scan_thread_.join();
}

// ======================== ONVIF 스캔 ========================

void OnvifScanner::runScan() {
    int sockfd;
    struct sockaddr_in multi_addr;
    
    // 1. UDP 소켓 생성
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        std::cerr << "ONVIF: Socket creation failed" << std::endl;
        return;
    }

    // 2. 수신 타임아웃 설정 (3초)
    struct timeval tv;
    tv.tv_sec = 3;
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // 2-1. 멀티캐스트 그룹 가입 (IGMP Snooping 환경 대응)
    // 기업용 스위치에서 IGMP Snooping이 활성화된 경우,
    // 명시적으로 가입하지 않으면 멀티캐스트 응답을 수신할 수 없음
    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr("239.255.255.250");
    mreq.imr_interface.s_addr = INADDR_ANY;
    if (setsockopt(sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        std::cerr << "ONVIF: Failed to join multicast group (IGMP)" << std::endl;
    }

    // 3. 멀티캐스트 목적지 설정 (239.255.255.250:3702)
    memset(&multi_addr, 0, sizeof(multi_addr));
    multi_addr.sin_family = AF_INET;
    multi_addr.sin_addr.s_addr = inet_addr("239.255.255.250");
    multi_addr.sin_port = htons(3702);

    std::cout << "ONVIF: Scanner thread started." << std::endl;

    while (*is_running_) {
        // 4. Probe 메시지 전송
        sendto(sockfd, ONVIF_PROBE_MSG.c_str(), ONVIF_PROBE_MSG.size(), 0,
               (struct sockaddr *)&multi_addr, sizeof(multi_addr));
        
        // 5. 응답 수집 루프 (3초간 유지)
        char buffer[4096];
        struct sockaddr_in cam_addr;
        socklen_t addr_len = sizeof(cam_addr);

        while (true) {
            int n = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0,
                             (struct sockaddr *)&cam_addr, &addr_len);
            
            if (n < 0) break; // 타임아웃 → 루프 탈출

            buffer[n] = '\0';
            std::string response(buffer);
            std::string cam_ip = inet_ntoa(cam_addr.sin_addr);

            if (response.find("ProbeMatch") != std::string::npos) {
                std::cout << "[ONVIF] ProbeMatch from: " << cam_ip << std::endl;

                // 이미 등록된 카메라인지 확인 (curl 호출 전에!)
                std::string check_id = "Hanwha_" + cam_ip + "_CH_0";
                if (is_registered_ && is_registered_(check_id)) {
                    continue;  // 이미 등록됨 → curl 스킵
                }

                // SUNAPI 2회 실패한 IP는 재시도 안 함
                if (sunapi_fail_count_[cam_ip] >= 2) {
                    continue;
                }

                // URL 수집 (오래 걸리는 작업)
                std::cout << "[ONVIF] Trying SUNAPI for: " << cam_ip << std::endl;
                std::vector<std::string> urls = getRtspUrls(cam_ip);
                if (urls.empty()) {
                    sunapi_fail_count_[cam_ip]++;
                    if (sunapi_fail_count_[cam_ip] >= 2) {
                        std::cout << "[ONVIF] SUNAPI 2회 실패 → " << cam_ip << " 재시도 차단" << std::endl;
                    }
                    continue;
                }

                // 각 채널을 콜백으로 DeviceManager에 알림
                for (int i = 0; i < (int)urls.size(); i++) {
                    std::string id = "Hanwha_" + cam_ip + "_CH_" + std::to_string(i);

                    DeviceInfo info;
                    info.id = id;
                    info.ip = cam_ip;
                    info.type = DeviceType::HANWHA;
                    info.source_url = urls[i];
                    info.is_online = true;
                    info.command_socket_fd = -1;

                    if (on_device_found_) {
                        on_device_found_(info);
                    }
                }
            }
        }

        // 6. ARP 폴백 스캔 (멀티캐스트로 못 찾은 한화 카메라 보완)
        arpFallbackScan();

        // 7. 다음 스캔까지 30초 휴식
        for(int i=0; i<30; ++i) {
            if(!(*is_running_)) break;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    close(sockfd);
    std::cout << "ONVIF: Scanner thread stopped." << std::endl;
}

// ======================== RTSP URL 추출 ========================

std::vector<std::string> OnvifScanner::getRtspUrls(const std::string& ip) {
    std::vector<std::string> urls;
    
    for (int ch = 0; ch < 4; ++ch) {
        std::string cmd = "curl --digest -u admin:5hanwha! -s --connect-timeout 2 "
                      "\"http://" + ip + "/stw-cgi/media.cgi?msubmenu=streamuri&action=view&Channel=" + std::to_string(ch) + "&Profile=2&MediaType=Live&Mode=Full&StreamType=RTPUnicast&TransportProtocol=TCP&RTSPOverHTTP=False\" "
                      "| grep \"URI\" | cut -d'=' -f2";

        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) break;

        char buffer[512];
        std::string result = "";
        if (fgets(buffer, sizeof(buffer), pipe) != NULL) {
            result = buffer;
        }
        pclose(pipe);

        // 줄바꿈 제거
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
            result.pop_back();
        }

        if (result.empty() || result.find("Error") != std::string::npos) {
            if (ch == 0) {
                std::cout << "[SmartCheck] Multi-channel failed for " << ip << ". Trying single mode..." << std::endl;
                std::string fallback_url = getSingleRtspUrl(ip);
                if (!fallback_url.empty()) {
                    urls.push_back(fallback_url);
                }
            }
            break; 
        }
        
        // 인증 정보 삽입
        size_t slash_pos = result.find("//");
        if (slash_pos != std::string::npos) {
            result.insert(slash_pos + 2, "admin:5hanwha!@");
        }

        urls.push_back(result);
        std::cout << "[SmartCheck] Found Channel " << (ch+1) << ": " << result << std::endl;
    }

    return urls;
}

std::string OnvifScanner::getSingleRtspUrl(const std::string& ip) {
    std::string cmd = "curl --digest -u admin:5hanwha! -s --connect-timeout 2 "
                      "\"http://" + ip + "/stw-cgi/media.cgi?msubmenu=streamuri&action=view&Profile=2&MediaType=Live&Mode=Full&StreamType=RTPUnicast&TransportProtocol=TCP&RTSPOverHTTP=False\" "
                      "| grep \"URI\" | cut -d'=' -f2";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";

    char buffer[512]; 
    std::string result = "";
    
    if (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        result = buffer;
    }
    pclose(pipe);

    if (!result.empty() && result.back() == '\n') {
        result.pop_back();
    }

    if (result.empty() || result.find("Error") != std::string::npos) {
        std::cout << "[SmartCheck] Single mode also failed for " << ip << std::endl;
        return "";
    }

    size_t slash_pos = result.find("//");
    if (slash_pos != std::string::npos) {
        result.insert(slash_pos + 2, "admin:5hanwha!@");
    }

    return result;
}

// ======================== ARP 폴백 스캔 ========================

void OnvifScanner::arpFallbackScan() {
    // 1. 유니캐스트 ping으로 ARP 테이블 갱신 (fping: 병렬 전송, ~2초)
    system("fping -a -g 192.168.0.1 192.168.0.254 -c 1 -q 2>/dev/null");

    // 2. ARP 테이블에서 한화 MAC 주소 필터링
    std::vector<std::string> hanwha_ips = getHanwhaIpsFromArp();

    for (const auto& ip : hanwha_ips) {
        if (!(*is_running_)) break;

        // 이미 등록된 카메라는 스킵
        std::string check_id = "Hanwha_" + ip + "_CH_0";
        if (is_registered_ && is_registered_(check_id)) {
            continue;
        }

        // SUNAPI 2회 실패한 IP는 재시도 안 함
        if (sunapi_fail_count_[ip] >= 2) {
            continue;
        }

        // SUNAPI로 RTSP URL 획득 시도
        std::cout << "[ARP Scan] Trying SUNAPI for: " << ip << std::endl;
        std::vector<std::string> urls = getRtspUrls(ip);
        if (urls.empty()) {
            sunapi_fail_count_[ip]++;
            if (sunapi_fail_count_[ip] >= 2) {
                std::cout << "[ARP Scan] SUNAPI 2회 실패 → " << ip << " 재시도 차단" << std::endl;
            }
            continue;
        }

        // 각 채널을 콜백으로 DeviceManager에 알림
        for (int i = 0; i < (int)urls.size(); i++) {
            std::string id = "Hanwha_" + ip + "_CH_" + std::to_string(i);

            DeviceInfo info;
            info.id = id;
            info.ip = ip;
            info.type = DeviceType::HANWHA;
            info.source_url = urls[i];
            info.is_online = true;
            info.command_socket_fd = -1;

            if (on_device_found_) {
                on_device_found_(info);
            }
        }
    }
}

std::vector<std::string> OnvifScanner::getHanwhaIpsFromArp() {
    std::vector<std::string> result;
    std::ifstream arp_file("/proc/net/arp");
    if (!arp_file.is_open()) return result;

    std::string line;
    std::getline(arp_file, line); // 헤더 스킵

    // /proc/net/arp 형식: IP address  HW type  Flags  HW address  Mask  Device
    while (std::getline(arp_file, line)) {
        std::istringstream iss(line);
        std::string ip, hw_type, flags, mac, mask, device;
        iss >> ip >> hw_type >> flags >> mac >> mask >> device;

        // 한화비전 OUI: e4:30:22
        if (mac.substr(0, 8) == "e4:30:22") {
            result.push_back(ip);
        }
    }

    return result;
}
