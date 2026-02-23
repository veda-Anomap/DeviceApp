#include <iostream>
#include <csignal>

#include "StreamController.h"

// 전역 포인터 (시그널 핸들러용)
StreamController* g_app = nullptr;

void signalHandler(int signum) {
    std::cout << "\n[StreamServer] Interrupt signal (" << signum << ") received.\n";
    if (g_app) g_app->stop();
}

int main() {
    signal(SIGINT, signalHandler);

    StreamController app;
    g_app = &app;

    app.run();

    return 0;
}
