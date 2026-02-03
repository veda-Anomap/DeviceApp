#include <iostream>

#include "PiConnection.h"

using namespace std;

PiConnection::PiConnection(){
    cout << "PiConnection called\n";
}

PiConnection::~PiConnection(){
    
}

void PiConnection::start(int port){

}

void PiConnection::runServer() {
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    // 1. 소켓 생성
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port_);

    // 2. 바인드 및 리슨
    bind(server_fd_, (struct sockaddr *)&address, sizeof(address));
    listen(server_fd_, 3);

    while (1) {
        // 3. AI 파이의 접속 대기
        int new_socket = accept(server_fd_, (struct sockaddr *)&address, (socklen_t*)&addrlen);
        if (new_socket < 0) continue;

        // 4. 데이터 수신 루프
        char buffer[1024] = {0};
        while (is_running_) {
            int valread = read(new_socket, buffer, 1024);
            if (valread <= 0) break; // 접속 끊김

            std::string receivedData(buffer, valread);
            if (data_callback_) {
                data_callback_(receivedData); // 데이터 전달!
            }
        }
        close(new_socket);
    }
}