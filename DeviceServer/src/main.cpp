#include <iostream>
#include <csignal>

#include "DeviceController.h"

DeviceController* g_app = nullptr;

void signalHandler(int signum) {
    std::cout << "\n[DeviceServer] Interrupt signal (" << signum << ") received.\n";
    if (g_app) g_app->stop();
}

int main() {
    signal(SIGINT, signalHandler);

    DeviceController app;
    g_app = &app;

    app.run();

    return 0;
}
