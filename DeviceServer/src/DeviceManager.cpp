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

//---------------------------------초기화, 정지, 생존여부------------------------

void DeviceManager::startDiscovery() {
    if (is_discovering_) return; // 이미 실행 중이면 무시
    is_discovering_ = true;
    is_monitoring_ = true;

    std::cout << "DeviceManager: Starting discovery threads..." << std::endl;
    
    // 10001번 포트 UDP 비콘 수신 스레드
    discovery_threads_.emplace_back(&DeviceManager::runBeaconReceiver, this);
    
    // ONVIF 장치 스캔 스레드 (주기적 실행)
    discovery_threads_.emplace_back(&DeviceManager::runOnvifScanner, this);

    // 장치 생존 여부 감시 스레드
    discovery_threads_.emplace_back(&DeviceManager::monitorLoop, this);
}

void DeviceManager::stopDiscovery() {
    is_discovering_ = false; // 스레드 루프를 멈추게 하는 플래그
    is_monitoring_ = false; // 모니터링 중지

    for (auto& t : discovery_threads_) {
        if (t.joinable()) {
            t.join(); // 스레드가 완전히 끝날 때까지 대기
        }
    }
    discovery_threads_.clear();
}

void DeviceManager::monitorLoop() {
    while (is_monitoring_) {
        // 3초마다 검사
        std::this_thread::sleep_for(std::chrono::seconds(3));

        std::map<std::string, DeviceInfo> devices_copy;
        {
            std::lock_guard<std::mutex> lock(device_mutex_);
            for (auto& pair : devices_) {
                devices_copy[pair.first] = pair.second;
            }
        }
        
        // 삭제할 장치들의 ID를 담을 리스트
        std::vector<std::string> disconnected_devices;

        for (auto& pair : devices_copy) {
            std::string device_id = pair.first;
            DeviceInfo& info = pair.second;

            // Sub-Pi인 경우에만 체크 (한화 카메라는 TCP 연결 방식이 다를 수 있음)
            // Sub-Pi인 경우에만 TCP 소켓 체크
            if (info.type == DeviceType::SUB_PI) {
                bool is_alive = true;

                // -----------------------------------------------------------
                // [체크 1] 상대방이 연결을 끊었는지(FIN) 확인 (가장 중요!)
                // recv()가 0을 반환하면 상대방이 소켓을 닫았다는 뜻입니다.
                // -----------------------------------------------------------
                char buffer[1];
                // MSG_PEEK: 데이터를 꺼내지 않고 살짝 보기만 함
                // MSG_DONTWAIT: 기다리지 않고 즉시 리턴
                int recv_result = recv(info.command_socket_fd, buffer, 1, MSG_PEEK | MSG_DONTWAIT);

                if (recv_result == 0) {
                    // 리턴값이 0이면 상대방이 정상 종료(Ctrl+C)한 것임
                    std::cout << "[Monitor] Detected Closed Connection (recv=0): " << device_id << std::endl;
                    is_alive = false;
                }
                else if (recv_result < 0) {
                    // 에러가 났는데, "읽을 데이터 없음(EAGAIN)"이 아니면 진짜 에러
                    if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        std::cout << "[Monitor] Recv Error: " << device_id << " (" << strerror(errno) << ")" << std::endl;
                        is_alive = false;
                    }
                }

                // -----------------------------------------------------------
                // [체크 2] 쓰기 테스트 (Ping) - 갑작스러운 전원 차단 감지용
                // -----------------------------------------------------------
                if (is_alive) {
                    int sent = send(info.command_socket_fd, nullptr, 0, MSG_NOSIGNAL | MSG_DONTWAIT);
                    if (sent < 0) {
                        if (errno != EAGAIN && errno != EWOULDBLOCK) {
                            std::cout << "[Monitor] Send Error (Broken Pipe): " << device_id << std::endl;
                            is_alive = false;
                        }
                    }
                }

                // 사망 확정 시 목록에 추가
                if (!is_alive) {
                    disconnected_devices.push_back(device_id);
                }
            }
        }
        // [청소] 죽은 장치들 목록에서 제거 (뮤텍스 필수!)
        if (!disconnected_devices.empty()) {
            std::lock_guard<std::mutex> lock(device_mutex_);
            for (const auto& id : disconnected_devices) {
                // 소켓 닫기
                if (devices_[id].command_socket_fd != -1) {
                    close(devices_[id].command_socket_fd);
                }
                // 맵에서 삭제
                devices_.erase(id);
            }
            std::cout << "[DeviceManager] Cleanup Complete. Remaining devices: " << devices_.size() << std::endl;
        }
    }
}

std::vector<DeviceInfo> DeviceManager::getDeviceList() {
    std::lock_guard<std::mutex> lock(device_mutex_); // [중요] 읽는 동안 쓰기 금지!
    
    std::vector<DeviceInfo> list;
    for (const auto& pair : devices_) {
        list.push_back(pair.second);
    }
    return list; // 안전하게 복사된 리스트 반환
}

//-------------------------------------------SUB PI--------------------------------------

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

            if (message.find("SUB_PI_ALIVE") != std::string::npos) {
                std::lock_guard<std::mutex> lock(device_mutex_);
                std::string id = "SubPi_" + sender_ip;
                int tcp_socket;
                // 이미 등록된 장치인지 확인
                if (devices_.find(id) == devices_.end()) {
                    // 1. 카메라에 TCP로 스트리밍 시작 명령 전송
                    if (requestStartStream(sender_ip, next_port, tcp_socket)) {
                        DeviceInfo info;
                        info.id = id;
                        info.ip = sender_ip;
                        info.type = DeviceType::SUB_PI; // Enum 사용
                        info.udp_listen_port = next_port++; // 포트 할당 후 증가
                        info.command_socket_fd = tcp_socket;
                        info.is_online = true;

                        devices_[id] = info;
                        std::cout << "Beacon: Registered Sub-Pi at " << sender_ip 
                                  << " (Port: " << info.udp_listen_port << ")" << std::endl;
                        
                        // 콜백 호출 (RTSP 릴레이 자동 생성 등)
                        if (on_device_registered_) {
                            on_device_registered_(info);
                        }

                        // AI 이벤트 리스너 스레드 생성 (Sub-Pi의 TCP 소켓에서 이벤트 대기)
                        listener_threads_.emplace_back(&DeviceManager::subPiListener, this, id, info.command_socket_fd);
                    }
                }
            }
        }
    }

    close(sockfd);
    std::cout << "Beacon: Thread stopped." << std::endl;
}

// [추가] TCP로 카메라에 명령을 보내는 헬퍼 함수
bool DeviceManager::requestStartStream(const std::string& target_ip, int listen_port, int& tcp_socket) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;
    tcp_socket = sock;

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(5000); // 카메라의 TCP 제어 포트 (예: 10002)
    inet_pton(AF_INET, target_ip.c_str(), &serv_addr.sin_addr);

    // 타임아웃 설정 (연결 시도 2초)
    struct timeval timeout = {2, 0};
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cerr << "[TCP] Connection failed to " << target_ip << std::endl;
        close(sock);
        return false;
    }

    // "나의 XX번 포트로 영상 쏴라"라는 메시지 전송
    std::string msg = "START_STREAM:" + std::to_string(listen_port);
    send(sock, msg.c_str(), msg.size(), 0);
    
    std::cout << "[TCP] Sent command to " << target_ip << " : " << msg << std::endl;
    
    return true;
}

// ---------------------------------------ONVIF-------------------------------

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
                
                // 1. [최적화] 이미 등록된 IP인지 먼저 가볍게 확인 (락 걸고)
                // 굳이 curl 또 날릴 필요 없으니까요.
                bool already_registered = false;
                {
                    std::lock_guard<std::mutex> lock(device_mutex_);
                    // 첫 번째 채널 ID가 있는지 확인해보면 됨
                    std::string check_id = "Hanwha_" + cam_ip + "_CH_0"; 
                    if (devices_.find(check_id) != devices_.end()) {
                        already_registered = true;
                    }
                } // 여기서 락 자동 해제됨

                // 이미 등록된 놈이면 무시하고 다음 패킷 기다림
                if (already_registered) continue;


                // 2. [오래 걸리는 작업] URL 수집 (락 없이 실행 -> UI 멈춤 방지!)
                std::vector<std::string> urls = getRtspUrls(cam_ip);
                if (urls.empty()) continue; // 실패시 패스


                // 3. [등록] 정보를 다 가져왔으니 이제 락 걸고 장부에 기록
                std::lock_guard<std::mutex> lock(device_mutex_);
                
                for (int i = 0; i < urls.size(); i++) {
                    std::string rtsp_url = urls[i]; // [수정] urls[0] -> urls[i]
                    
                    // ID 생성 (Hanwha_192.168.0.40_CH_0 ...)
                    std::string id = "Hanwha_" + cam_ip + "_CH_" + std::to_string(i);

                    if (rtsp_url.empty()) break; // 혹시 빈 문자열이면 중단

                    // (아까 확인했지만 더블 체크)
                    if (devices_.find(id) == devices_.end()) {
                        DeviceInfo info;
                        info.id = id;
                        info.ip = cam_ip;
                        info.type = DeviceType::HANWHA;
                        info.source_url = rtsp_url;
                        info.is_online = true;

                        devices_[id] = info;
                        std::cout << "ONVIF: Registered Hanwha Camera: " << id << std::endl;

                        if (on_device_registered_) {
                            on_device_registered_(info);
                        }
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

std::vector<std::string> DeviceManager::getRtspUrls(const std::string& ip) {
    std::vector<std::string> urls;
    
    // 최대 4채널까지만 찔러본다고 가정 (필요하면 16으로 늘리세요)
    for (int ch = 0; ch < 4; ++ch) {
        
        // Channel 파라미터를 0, 1, 2, 3... 으로 바꿔가며 요청
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
        // 결과가 비어있거나 에러면 -> 더 이상 채널이 없다는 뜻 -> 루프 종료
        if (result.empty() || result.find("Error") != std::string::npos) {
            
            // 만약 "첫 번째(0번)" 시도부터 에러가 났다면?
            // -> "아, 얘는 NVR 방식(Channel=X)이 안 먹히는 놈이구나!"
            // -> 그때만 1채널 전용 명령어를 시도한다.
            if (ch == 0) {
                std::cout << "[SmartCheck] Multi-channel failed. Trying single mode..." << std::endl;
                std::string fallback_url = getSingleRtspUrl(ip); // 함수 이름은 getSingleChannelUrl 추천
                if (!fallback_url.empty()) {
                    urls.push_back(fallback_url);
                }
            }
            
            // ch가 1, 2, 3일 때 에러가 났다면?
            // -> "아, 얘는 채널이 여기까지구나." 하고 그냥 쿨하게 종료.
            break; 
        }
        
        // 만약 결과가 "rtsp://192.168..." 로 시작하면 "rtsp://admin:5hanwha!@192.168..." 로 끼워넣기
        size_t slash_pos = result.find("//");
        if (slash_pos != std::string::npos) {
            result.insert(slash_pos + 2, "admin:5hanwha!@");
        }

        urls.push_back(result);
        std::cout << "[SmartCheck] Found Channel " << (ch+1) << ": " << result << std::endl;
    }

    return urls;
}

std::string DeviceManager::getSingleRtspUrl(const std::string& ip) {
    // 1. 결과만 딱 뽑아오는 리눅스 명령어 조합
    // (따옴표 처리에 주의하세요: C++ 문자열 안에서 쌍따옴표는 \" 로 써야 함)
    std::string cmd = "curl --digest -u admin:5hanwha! -s --connect-timeout 2 "
                      "\"http://" + ip + "/stw-cgi/media.cgi?msubmenu=streamuri&action=view&Profile=2&MediaType=Live&Mode=Full&StreamType=RTPUnicast&TransportProtocol=TCP&RTSPOverHTTP=False\" "
                      "| grep \"URI\" | cut -d'=' -f2";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return ""; // 실행 실패 시 기본값

    char buffer[512]; 
    std::string result = "";
    
    // 2. 결과 읽기
    if (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        result = buffer;
    }
    pclose(pipe);

    // [보완 1] 치명적인 줄바꿈(\n) 문자 제거 (핵심!)
    if (!result.empty() && result.back() == '\n') {
        result.pop_back();
    }

    if (result.empty() || result.find("Error") != std::string::npos) {   
        return "";
    }

    size_t slash_pos = result.find("//");
    if (slash_pos != std::string::npos) {
        result.insert(slash_pos + 2, "admin:5hanwha!@");
    }

    return result;
}

// ------------------------------생성, 소멸자------------------------------

DeviceManager::DeviceManager() : is_discovering_(false) {
    std::cout << "[DEBUG] DeviceManager Created." << std::endl;
}

DeviceManager::~DeviceManager() {
    stopDiscovery();
    // 리스너 스레드 정리
    for (auto& t : listener_threads_) {
        if (t.joinable()) t.join();
    }
}

// ======================== Sub-Pi AI 이벤트 리스너 ========================

void DeviceManager::subPiListener(std::string device_id, int socket_fd) {
    std::cout << "[AI Listener] Started for " << device_id << " (fd: " << socket_fd << ")" << std::endl;

    while (is_discovering_) {
        // 1. 헤더 수신 (5바이트: Type + BodyLength)
        PacketHeader header;
        if (!recvExact(socket_fd, &header, sizeof(header))) {
            std::cout << "[AI Listener] " << device_id << " disconnected." << std::endl;
            break;
        }

        uint32_t body_len = ntohl(header.body_length);
        if (body_len == 0 || body_len > 1024 * 1024) continue;

        // 2. JSON 본문 수신
        std::vector<char> buf(body_len);
        if (!recvExact(socket_fd, buf.data(), body_len)) break;

        // 3. AI 타입인 경우 콜백 호출
        if (header.type == MessageType::AI) {
            try {
                json event = json::parse(std::string(buf.begin(), buf.end()));
                event["device_id"] = device_id;  // 어느 장치에서 났는지 추가

                std::cout << "[AI Listener] Event from " << device_id << ": " << event.dump() << std::endl;

                if (on_ai_event_) {
                    on_ai_event_(device_id, event);
                }
            } catch (const json::parse_error& e) {
                std::cerr << "[AI Listener] JSON parse error: " << e.what() << std::endl;
            }
        }
    }

    std::cout << "[AI Listener] Stopped for " << device_id << std::endl;
}

bool DeviceManager::recvExact(int fd, void* buf, size_t len) {
    size_t received = 0;
    char* ptr = static_cast<char*>(buf);
    while (received < len) {
        ssize_t n = recv(fd, ptr + received, len - received, 0);
        if (n <= 0) return false;
        received += n;
    }
    return true;
}