#ifndef _APPCONTROLLER_H
#define _APPCONTROLLER_H

#include "RtspServer.h"

class AppController {
private:
    RtspServer rtsp_server_;
public:
    AppController();
    ~AppController();
    void run(int argc, char* argv[]);    
};
#endif