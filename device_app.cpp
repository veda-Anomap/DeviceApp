#include <iostream>
#include <csignal>

#include "AppController.h"

// 전역 포인터 (시그널 핸들러용)
AppController* g_app = nullptr;

// Ctrl+C 눌렀을 때 안전하게 종료하기 위한 핸들러
void signalHandler(int signum) {
    std::cout << "\nInterrupt signal (" << signum << ") received.\n";
    if (g_app) {
        g_app->stop();
    }
    // 루프가 깨지고 나서 main 함수가 리턴되도록 유도
}

int main() {
    // 시그널 등록
    signal(SIGINT, signalHandler);

    AppController app;
    g_app = &app;

    try {
        app.run(); // 여기서 무한 루프 돎
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }

    return 0;
}