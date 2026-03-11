#ifndef NET_UTIL_H
#define NET_UTIL_H

#include <sys/socket.h>
#include <cstddef>

// ======================== 네트워크 I/O 헬퍼 ========================
// header-only: #include만으로 사용 가능 (Makefile 변경 불필요)

// N바이트를 정확히 전송 (partial write 방지)
inline bool sendExact(int fd, const void* buf, size_t len) {
    const char* ptr = static_cast<const char*>(buf);
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, ptr + sent, len - sent, MSG_NOSIGNAL);
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

// N바이트를 정확히 수신
inline bool recvExact(int fd, void* buf, size_t len) {
    char* ptr = static_cast<char*>(buf);
    size_t received = 0;
    while (received < len) {
        ssize_t n = recv(fd, ptr + received, len - received, 0);
        if (n <= 0) return false;
        received += n;
    }
    return true;
}

#endif // NET_UTIL_H
