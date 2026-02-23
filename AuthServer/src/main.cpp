#include <iostream>
#include <csignal>

#include "AuthController.h"

// 전역 포인터 (시그널 핸들러용)
AuthController* g_app = nullptr;

void signalHandler(int signum) {
    std::cout << "\n[AuthServer] Interrupt signal (" << signum << ") received.\n";
    if (g_app) g_app->stop();
}

int main() {
    signal(SIGINT, signalHandler);

    AuthController app;
    g_app = &app;

    app.run();

    return 0;
}
