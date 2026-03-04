#ifndef QT_COMM_SERVER_H
#define QT_COMM_SERVER_H

#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <vector>
#include <map>
#include <mutex>
#include <set>

#include "Common.h"
#include "json.hpp"

using json = nlohmann::json;

// Qt 클라이언트로부터 메시지를 받았을 때 호출되는 콜백 타입
// (클라이언트 소켓 fd, 메시지 타입, JSON 본문)
using QtMessageCallback = std::function<void(int client_fd, MessageType type, const json& body)>;

class QtCommServer {
public:
    QtCommServer();
    ~QtCommServer();

    // 서버 시작 (지정된 포트에서 TCP 대기)
    void start(int port, QtMessageCallback callback);
    void stop();

    // 특정 클라이언트에게 메시지 전송
    bool sendMessage(int client_fd, MessageType type, const json& body);

    // 연결된 모든 클라이언트에게 브로드캐스트
    void broadcast(MessageType type, const json& body);

    // role 기반 브로드캐스트 ("user" = user+admin, "admin" = admin만)
    void broadcastToRole(MessageType type, const json& body, const std::string& min_role);

    // IMAGE 전송 (PacketHeader + JSON 메타 + JPEG 바이너리)
    bool sendImageMessage(int client_fd, const json& meta, const std::vector<char>& jpeg);

    // 전체 클라이언트에 IMAGE 브로드캐스트
    void broadcastImage(const json& meta, const std::vector<char>& jpeg);

    // 클라이언트 role 설정 (로그인 성공 시 호출)
    void setClientRole(int client_fd, const std::string& role);

    // 현재 연결된 클라이언트 수
    int getClientCount();

private:
    // 클라이언트 접속 수락 루프
    void acceptLoop();

    // 개별 클라이언트 처리 루프
    void clientHandler(int client_fd);

    // 끝난 스레드를 정리하는 함수 (스레드 누수 방지)
    void cleanupFinishedThreads();

    // N바이트를 정확히 읽는 헬퍼 함수
    bool recvExact(int fd, void* buf, size_t len);

    // 서버 소켓
    int server_fd_ = -1;
    int port_ = 20000;

    // 스레드 관리
    std::thread accept_thread_;
    std::map<int, std::thread> client_threads_;  // fd → thread 매핑
    std::atomic<bool> is_running_{false};

    // 연결된 클라이언트 fd 목록 (브로드캐스트용)
    std::vector<int> client_fds_;
    std::mutex client_mutex_;

    // 클라이언트 역할 관리 (fd → role: "admin"/"user"/"")
    std::map<int, std::string> client_roles_;

    // 끝난 클라이언트 fd 목록 (스레드 정리용)
    std::set<int> finished_fds_;
    std::mutex finished_mutex_;

    // 메시지 수신 콜백
    QtMessageCallback on_message_;
};

#endif // QT_COMM_SERVER_H