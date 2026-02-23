#include <iostream>
#include <csignal>

#include "ClientController.h"

ClientController* g_app = nullptr;

void signalHandler(int signum) {
    std::cout << "\n[ClientServer] Interrupt signal (" << signum << ") received.\n";
    if (g_app) g_app->stop();
}

int main() {
    signal(SIGINT, signalHandler);

    ClientController app;
    g_app = &app;

    app.run();

    return 0;
}
